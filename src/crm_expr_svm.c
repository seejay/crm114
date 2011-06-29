//	crm_svm.c - Support Vector Machine

////////////////////////////////////////////////////////////////////////
//    This code is originally copyright and owned by William
//    S. Yerazunis.  In return for addition of significant derivative
//    work, Yimin Wu is hereby granted a full unlimited license to use
//    this code, includng license to relicense under other licenses.
////////////////////////////////////////////////////////////////////////
//
// Copyright 2009 William S. Yerazunis.
// This file is under GPLv3, as described in COPYING.

//  include some standard files
#include "crm114_sysincludes.h"

//  crm_expr_sks.c specific system include file
#include <time.h>

//  include any local crm114 configuration file
#include "crm114_config.h"

//  include the crm114 data structures file
#include "crm114_structs.h"

//  and include the routine declarations file
#include "crm114.h"

//    the globals used when we need a big buffer  - allocated once, used
//    wherever needed.  These are sized to the same size as the data window.
extern char *tempbuf;

////////////////////////////////////////////////////////////////////////
//
//                  Support Vector Machine (SVM) Classification
//
//    This is an implementation of a support vector machine classification.
//    The current version only implement one type of SVM called C-Support
//    Vector Classification (C-SVC, Boser et al., 1992; Cortes and Vapnik,
//    1995).
//
//    The dual formulation of C-SVC is to find
//
//	      min     0.5 ( \alpha^T Q \alpha) - e^T \alpha
//
//	subject to    y^T \alpha = 0
//		      y_i = +1 or -1
//		      0 <= alpha_i <= C, i=1,...,sizeof(corpus).
//
//    Where "e" is the vector of all ones,
//       Q is the sizeof(corpus) by sizeof(corpus) matrix containing the
//         calculated distances between any two documents (that is,
//           Q_ij = y_i * y_j * kernel(x_i, x_j) which may be HUGE and so
//           we only calculate part of it at any one time.
//       x_i is the feature vector of document i.
//
//    The decision function is
//
//           sgn (sum(y_i * \alpha_i * kernel(x_i, x)) + b)
//
//     In the optimization, we set the kernel parameters at the start and
//     then modify only the weighting parameters till it (hopefully) converges.

////////////////////////////////////////////////////////////////////////
//
//                   SMO-type Decomposition Method
//
//    Here we used SMO-type decomposition method ( Platt, 1998) to solve
//    the quadratic optimization problem --dual formulation of C-SVC, using
//    the method of Fan, Chen, and Lin ("Working Set Selection using Second
//    Order Information for Training Support Vector Machines", 2005)
//    to select the working set.
//

//   Pick a kernel...
#define LINEAR 0
#define RBF 1
#define POLY 2

//   and a mode for the software to run in (only C-SVC works for now)
#define C_SVC 0
#define ONE_CLASS 1

//    Tau is a small minimum positive number (divide-by-zero noodge)
#define TAU 1e-12


//     Parameter block to control the SVM solver.
//
typedef struct mythical_svm_param {
  int svm_type;
  int kernel_type;
  double cache_size;          // in MB
  double eps;                 // convergence stop criterion
  double C;                   // parameter in C_SVC
  double nu;                  // parameter for One-class SVM
  double max_run_time;        // time control for microgroom (in seconds).
                              // If computing time exceeds max_run_time,
                              // then start microgrooming to delete the
                              //  documents far away from the hyperplane.
  int shrinking;        // use the shrinking heuristics, isn't available now
} SVM_PARAM;


//     And a struct to hold the actual data we're trying to solve.
//
typedef struct mythical_svm_problem {
  int l;          //number of documents
  int *y;         //label of documents -1/+1
  unsigned int **x;   //  x[i] is the ith document's
                      //  feature vector
} SVM_PROBLEM;

//    A structure to hold the cache node - these hold one row worth of
//    Q matrix.
//
typedef struct mythical_cache_node {
  struct mythical_cache_node *prev, *next;
  float *data;
  int len;
} CACHE_NODE;

//    This is the cache representaton of the whole matrix; this is the
//    "first column" and points to the start of each row.
typedef struct mythical_cache {
  int l; //the number of documents in the corpus
  long size; //the cache size (bytes)
  CACHE_NODE  *head;
  CACHE_NODE lru_headnode; //least-recent-use node
} CACHE;

//   This stores the result - alpha is the weighting vector (what we are
//   searching for) and
//
typedef struct mythical_solver{
  double *alpha;
  double *G;            // Gradient of objective function in each dimension
  double *deci_array;   // decision values for all training data
} SOLVER;

//   And a few file-wide static globals:
//
static SVM_PARAM param;
static SVM_PROBLEM svm_prob;
static CACHE svmcache;
static float *DiagQ;     //diagonal Qmatrix
static SOLVER solver;

static int hash_compare (void const *a, void const *b)
{
  unsigned int *pa, *pb;
  pa = (unsigned int *) a;
  pb = (unsigned int *) b;
  if (*pa < *pb)
    return (-1);
  if (*pa > *pb)
    return (1);
  return (0);
}

//   Compare two integers when all we have are void* pointers to them
static int int_compare (void const *a, void const *b)
{
  // We can do this because the data types all match.
  // The two numbers being compared, the value of this expression,
  // and the return value of this function are all int.
  return (*(int *)a - *(int *)b);
}

//  Compare ids of documents according to its decision value (decending
//  absolute value), which is used for microgrooming
static int descend_int_decision_compare(void const *a, void const *b)
{
  int *pa, *pb;
  pa = (int *) a;
  pb = (int *) b;
  if ( solver.deci_array[*pa] < solver.deci_array[*pb])
    return (1);
  if ( solver.deci_array[*pa] > solver.deci_array[*pb])
    return (-1);
  return (0);
}

////////////////////////////////////////////////////////////////////////
//
//     Cache with least-recent-use strategy
//     This will be used to store the part of the Q matrix that we know
//     about.  We recalculate parts as needed... this lets us solve the
//     problem without requiring enough memory to build the entire Q
//     matrix.
//

static void cache_init ( int len, long size, CACHE *svmcache)
{
  svmcache->l = len;
  svmcache->size = size;
  svmcache->head = (CACHE_NODE *)calloc(len, sizeof(CACHE_NODE));
  // ??? size is local (arg) and not used again, so why mess with it?
  size /= sizeof(float);
  size -= len * (sizeof(CACHE_NODE)/sizeof(float));	// ???
  if(size < (2 * len))
    size = 2 * len;       //   cache size must at least
                          //   as large as two columns of Qmatrix
  (svmcache->lru_headnode).prev
    = (svmcache->lru_headnode).next
    = &(svmcache->lru_headnode);
}


//    Release the whole Q-matrix cache
//
static void cache_free(CACHE *svmcache)
{
  CACHE_NODE *temp;
  for(temp = (svmcache->lru_headnode).next;
      temp != &(svmcache->lru_headnode);
      temp = temp->next)
    free(temp->data);
  free(svmcache->head);
}


// Delete one node (that is, one row of Q-matrix) from the LRU.
// (we then usually move that row to the front of the LRU.
static void lru_delete(CACHE_NODE *h){
  h->prev->next = h->next;
  h->next->prev = h->prev;
}

// Insert to the last position in the cache node list
//
static void lru_insert(CACHE_NODE *h, CACHE *svmcache)
{
  h->next = &(svmcache->lru_headnode);
  h->prev = (svmcache->lru_headnode).prev;
  h->prev->next = h;
  (svmcache->lru_headnode).prev = h;
}

//  Get a line of Q matrix data for certain document, and return the
//  length of cached data.  If it is smaller than the request length,
//  then we need to fill in the uncached data.

static int get_data (CACHE *svmcache,
		     const int doc_index,
		     float **data,
		     int length)
{
  int result = length;
  CACHE_NODE *doc = svmcache->head + doc_index;
  if(doc->len) lru_delete(doc); //least-recent-use strategy

  //need to allocate more space
  if ( length > (doc->len))
    {
      //   GROT GROT GROT check this to see if it doesn't leak memory
      //  cache hasn't enough free space, we need to release some old space
      while((svmcache->size) < (length - doc->len))
	{
	  CACHE_NODE *temp = (svmcache->lru_headnode).next;
	  lru_delete(temp);
	  free(temp->data);
	  svmcache->size += temp->len;
	  temp->data = 0;
	  temp->len = 0;
	}
      //allocate new space
      doc->data = (float *)realloc(doc->data, length * sizeof(float));
      svmcache->size -= (length - doc->len);
      result = doc->len;
      doc->len = length;
    }
  lru_insert(doc, svmcache);
  *data = doc->data;
  return result;
}


// Dot operation of two feature vectors
//
static double dot(void const *a, void const *b)
{
  unsigned int *pa, *pb;
  int j = 0;
  int i = 0;
  double sum = 0;
  pa = (unsigned int *) a;
  pb = (unsigned int *) b;
  while(pa[i] != 0 && pb[j] != 0)
  {
    if(pa[i] == pb[j] && pa[i] != 0)
      {
	sum ++;
	i++;
	j++;
      }
    else
      {
	if(pa[i] > pb[j])
	  j++;
	else
	  i++;
      }
  }
  return sum;
}

//   RBF (Radial Basis Function) kernel
//
static double rbf ( void const *a, void const *b )
{
  unsigned int *pa, *pb;
  int j = 0;
  int i = 0;
  double sum = 0;
  pa = (unsigned int *) a;
  pb = (unsigned int *) b;
  while(pa[i] != 0 && pb[j] != 0)
    {
      if(pa[i] > pb[j])
	{
	  sum ++;
	  j++;
	}
      else if(pa[i] < pb[j])
	{
	  sum ++;
	  i++;
	}
      else
	{
	  i++;
	  j++;
	}
    }
  while(pa[i] != 0)
    {
      sum ++;
      i++;
    }
  while(pb[j] != 0)
    {
      sum ++;
      j++;
    }
  return exp(-0.00001  * sum);
}


//    Polynomial kernel of two basis vectors
static double poly(void const *a, void const *b)
{
  double gamma = 0.001;
  double sum = 0.0;
  sum = pow(gamma * dot(a,b) + 3, 2.0);
  return sum;
}


// Big switch to pick the kernel operation we want
static double kernel(void const *a, void const *b)
{
  switch(param.kernel_type)
    {
    case LINEAR:
      return dot(a,b);
    case RBF:
      return rbf(a,b);
    case POLY:
      return poly(a,b);
    default:
      return 0;
    }
}



//  Ask the cache for the ith row in the Q matrix for C-Support Vector
//  Classification(C-SVC)and one-class SVM
//
static float *get_rowQ(int i, int length)
{
  float *rowQ;
  int cached;
  if ((cached = get_data(&svmcache, i, &rowQ, length)) < length)
    {
      int temp;
      for (temp = cached; temp < length; temp++)
	{
	  if (param.svm_type == C_SVC)
	    //   multiply by the +1/-1 labels (in the .y structures) to
	    //   face the kernel result in the right direction.
	    rowQ[temp] = (float)svm_prob.y[i]
	      * svm_prob.y[temp]
	      * kernel(svm_prob.x[i],svm_prob.x[temp] ) ;
	  else if(param.svm_type == ONE_CLASS)
	    rowQ[temp] = (float)kernel(svm_prob.x[i],svm_prob.x[temp]);
	}
    }
  return rowQ;
}

//  Request of the diagonal in Qmatrix for C- Support Vector
//  Classification(C-SVC) and one-class SVM
static float *get_DiagQ()
{
  float *DiagQ = (float *) malloc(svm_prob.l * sizeof(float));
  int i;
  for(i = 0; i<svm_prob.l; i++)
  {
    DiagQ[i] = (float) kernel(svm_prob.x[i],svm_prob.x[i]);
  }
  return DiagQ;
}

//  Initialize the cache and diagonal Qmatrix
static void Q_init()
{
  //   Default initialization is the param cache size in megabytes (that's
  //   the 1<<20)
  cache_init(svm_prob.l, param.cache_size*(1<<20), &svmcache);
  DiagQ = get_DiagQ();
}

//   Now, select a working set.  This is based on the paper:
// "An SMO algorithm in Fan et al., JMLR 6(2005), p. 1889--1918"
// Solves:
//
//	min 0.5(\alpha^T Q \alpha) + p^T \alpha
//
//		y^T \alpha = \delta
//		y_i = +1 or -1
//		0 <= alpha <= C
//
//
// Given:
//
//	Q, p, y, C, and an initial feasible point \alpha
//	l is the size of vectors and matrices
//	eps is the stopping tolerance
//
// solution will be put in \alpha
//

//  modification from Fan's paper- select_times keeps us from re-selecting
//   the same document over and over (which was causing us to lock up)

static void selectB(int workset[], int *select_times)
{
  // select i
  int i = -1;
  double G_max;
  double G_min;
  int t, j;
  double obj_min;
  double a,b;
  float *Qi;

  //     Select a document that is on the wrong side of the hyperplane
  //    (called a "violating pair" in Fan's paper).  Note that the
  //    margin is not symmetrical - we can select any "positive" class
  //    element with alpha < param.C, but the 'negative' class must
  //    only be greater than 0.  Yimin says this is OK, I say it's
  //    wierd.
  G_max = - HUGE_VAL;
  for (t = 0; t < svm_prob.l; t++)
    {
      if((((svm_prob.y[t] == 1) && (solver.alpha[t] < param.C))
	  || ((svm_prob.y[t] == -1) && (solver.alpha[t] > 0)))
	 && select_times[t] < 10)
	{
	  if(-svm_prob.y[t] * solver.G[t] >= G_max)
	    {
	      i = t;
	      G_max = -svm_prob.y[t] * solver.G[t];
	    }
	}
    }
  //  select j as second member of working set;
  j = -1;
  obj_min = HUGE_VAL;
  G_min = HUGE_VAL;
  for (t = 0; t< svm_prob.l; t++)
    {
      if((((svm_prob.y[t] == -1) && (solver.alpha[t] < param.C))
	  || ((svm_prob.y[t] == 1) && (solver.alpha[t] > 0)))
	 && select_times[t] < 10)
	{
	  b = G_max + svm_prob.y[t] * solver.G[t];
	  if (-svm_prob.y[t] * solver.G[t] <= G_min)
	    G_min = -svm_prob.y[t] * solver.G[t];
	  if (b > 0)
	    {
	      if(i != -1)
		{
		  Qi = get_rowQ(i,svm_prob.l);
		  a = Qi[i] + DiagQ[t]
		    - 2 * svm_prob.y[i] * svm_prob.y[t] * Qi[t];
		  if (a <= 0)
		    a = TAU;
		  if(-(b * b) / a <= obj_min)
		    {
		      j = t;
		      obj_min = -(b * b) / a;
		    }
		}

	    }
	}
    }
  //   Are we done?
  if(G_max - G_min < param.eps)
    {
      workset[0] = -1;
      workset[1] = -1;
    }
  else
    {
      workset[0] = i;
      workset[1] = j;
    }
}


static void solve()
{
  int t,workset[2],i,j, n;
  double a, b, oldi, oldj, sum;
  float *Qi, *Qj;

  //  Array for storing how many times a particular document has been
  //  selected in working set.
  int select_times[svm_prob.l];
  for(i = 0; i < svm_prob.l; i++)
    {
      select_times[i] = 0;
    }

  solver.alpha = (double *)malloc(svm_prob.l * sizeof(double));
  solver.G = (double *)malloc(svm_prob.l * sizeof(double));
  if(param.svm_type == C_SVC)
    {
      // initialize alpha to all zero;
      // initialize G to all -1;
      for(t = 0; t < svm_prob.l; t++){
	solver.alpha[t] = 0;
	solver.G[t] = -1;
      }
    }
  else
    if
      (param.svm_type == ONE_CLASS)
      {
	//initialize the first nu*l elements of alpha to have the value one;
	n = (int)(param.nu * svm_prob.l);
	for(i = 0; i < n; i++)
	  solver.alpha[i] = 1;
	if(n < svm_prob.l)
	  solver.alpha[n] = param.nu * svm_prob.l - n;
	for(i = n + 1;i < svm_prob.l;i++)
	  solver.alpha[i] = 0;
	//initialize G to all 0;
	for(i = 0; i < svm_prob.l; i++){
	  solver.G[i] = 0;
	}
      };
  while(1)
    {
      selectB(workset, select_times);
      i = workset[0];
      j = workset[1];
      if(i != -1)
	select_times[i] ++;
      if(j != -1)
	select_times[j] ++;
      if(j == -1)
	break;
      //fprintf(stderr, "i=%d\tj=%d\t",i,j);

      Qi = get_rowQ(i, svm_prob.l);
      Qj = get_rowQ(j, svm_prob.l);

      //  Calculate the incremental step forward.
      a = Qi[i] + DiagQ[j] - 2 * svm_prob.y[i] * svm_prob.y[j] * Qi[j];
      if(a <= 0)
	a = TAU;
      b = -svm_prob.y[i] * solver.G[i] + svm_prob.y[j] * solver.G[j];

      //  update alpha (weight vector)
      oldi = solver.alpha[i];
      oldj = solver.alpha[j];
      solver.alpha[i] += svm_prob.y[i] * b/a;
      solver.alpha[j] -= svm_prob.y[j] * b/a;

      //  project alpha back to the feasible region (that is, where
      //  where 0 <= alpha <= C )
      sum = svm_prob.y[i] * oldi + svm_prob.y[j] * oldj;
      if (solver.alpha[i] > param.C)
	solver.alpha[i] = param.C;
      if (solver.alpha[i] < 0 )
	solver.alpha[i] = 0;
      solver.alpha[j] = svm_prob.y[j]
	* (sum - svm_prob.y[i] * (solver.alpha[i]));
      if (solver.alpha[j] > param.C)
	solver.alpha[j] = param.C;
      if (solver.alpha[j] < 0 )
	solver.alpha[j] = 0;
      solver.alpha[i] = svm_prob.y[i]
	* (sum - svm_prob.y[j] * (solver.alpha[j]));

      //  update gradient array
      for(t = 0; t < svm_prob.l; t++)
	{
	  solver.G[t] += Qi[t] * (solver.alpha[i] - oldi)
	    + Qj[t] * (solver.alpha[j] - oldj);
	}
    }

  //#define PRINT_OUT_ALPHAS 
#ifdef PRINT_OUT_ALPHAS
    {
      fprintf (stderr, "Order of SVM: %d\n Weight vector: \n", svm_prob.l);
      for (i = 0; i < svm_prob.l; i++)
	fprintf (stderr, "   example %d weight %f\n", i, solver.alpha[i]);
    };
#endif
}

//    Calculate b (hyperplane offset in
//      SUM (y[i] alpha[i] kernel (x[i],x)) + b form)
//       after calculating error margin alpha
static double calc_b()
{
  int count = 0;
  double upper = HUGE_VAL;
  double lower = -HUGE_VAL;
  double sum = 0;
  int i;
  double b;
  for(i = 0; i < svm_prob.l; i++)
    {
      if(svm_prob.y[i] == 1)
	{
	  if(solver.alpha[i] == param.C)
	    {
	      if(solver.G[i] > lower)
		{
		  lower = solver.G[i];
		}
	    }
	  else if(solver.alpha[i] == 0)
	    {
	      if(solver.G[i] < upper)
		{
		  upper = solver.G[i];
		}
	    }
	  else
	    {
	      count++;
	      sum += solver.G[i];
	    }
	}
      else
	{
	  if(solver.alpha[i] == 0)
	    {
	      if(-solver.G[i] > lower)
		{
		  lower = -solver.G[i];
		}
	    }
	  else if(solver.alpha[i] == param.C)
	    {
	      if(-solver.G[i] < upper)
		{
		  upper = -solver.G[i];
		}
	    }
	  else
	    {
	      count++;
	      sum -= solver.G[i];
	    }
	}
    }
  if(count > 0)
    b = -sum/count;
  else
    b = -(upper + lower)/2;
  return b;
}

//   Calculate the decision function
static double calc_decision (unsigned int *x,
			     double *alpha,
			     double b)
{
  int i;
  double sum = 0;
  i=0;
  if(param.svm_type == C_SVC)
    {
      for (i = 0; i < svm_prob.l; i++){
	if(alpha[i] != 0)
	  sum += svm_prob.y[i] * alpha[i] * kernel(x,svm_prob.x[i]);
      }
      sum += b;
    }
  else if(param.svm_type == ONE_CLASS)
    {
      for (i = 0; i < svm_prob.l; i++)
	{
	  if(alpha[i] != 0)
	    sum += alpha[i] * kernel(x,svm_prob.x[i]);
	}
      sum -= b;
    }
  return sum;
}

//  Implementation of Lin's 2003 improved algorithm on Platt's
//  probabilistic outputs for binary SVM input parameters: deci_array =
//  array of svm decision values svm.prob posn = number of positive
//  examples negn = number of negative examples

//  Outputs: parameters of sigmoid function-- A and B  (AB[0] = A, AB[1] = B)
static void calc_AB(double *AB, double *deci_array, int posn, int negn)
{
  int maxiter, i, j;
  double minstep, sigma, fval, hiTarget, loTarget, *t;
  double fApB, h11, h22, h21, g1, g2, p, q, d1, d2, det, dA, dB, gd, stepsize, newA, newB, newf;

  maxiter = 100;
  minstep = 1e-10;
  sigma = 1e-3;
  fval = 0.0;
  hiTarget = (posn + 1.0) / (posn + 2.0);
  loTarget = 1 / (negn + 2.0);
  t = (double *)malloc(svm_prob.l * sizeof(double));
  for(i = 0; i< svm_prob.l; i++)
    {
      if(svm_prob.y[i] > 0)
	t[i] = hiTarget;
      else
	t[i] = loTarget;
    }
  AB[0] = 0.0;
  AB[1] = log((negn + 1.0) / (posn + 1.0));
  for (i = 0; i < svm_prob.l; i++)
    {
      fApB = deci_array[i] * AB[0] + AB[1];
      if(fApB >= 0)
	fval += t[i] * fApB + log(1 + exp(-fApB));
      else
	fval += (t[i] - 1) * fApB + log(1 + exp(fApB));
    }

  for(j = 0; j < maxiter; j++)
    {
      h11 = h22 = sigma;
      h21 = g1 = g2 = 0.0;
      for(i = 0; i < svm_prob.l; i++)
	{
	  fApB = deci_array[i] * AB[0] + AB[1];
	  if(fApB >= 0)
	    {
	      p = exp(-fApB) / (1.0 + exp(-fApB));
	      q = 1.0 / (1.0 + exp(-fApB));
	    }
	  else
	    {
	      p =  1.0 / (1.0 + exp(fApB));
	      q = exp(fApB) / (1.0 + exp(fApB));
	    }
	  d2 = p * q;
	  h11 += deci_array[i] * deci_array[i] * d2;
	  h22 += d2;
	  h21 += deci_array[i] * d2;
	  d1 = t[i] - p;
	  g1 += deci_array[i] * d1;
	  g2 += d1;
	}
      // Stopping Criterion
      if ((fabs(g1) < 1e-5) && (fabs(g2) < 1e-5))
	{
	  break;
	}
      //compute modified Newton directions
      det = h11 * h22 - h21 * h21;
      dA = -(h22 * g1 - h21 * g2) / det;
      dB = -(-h21 * g1 +  h11 * g2) / det;
      gd = g1 * dA + g2 * dB;
      stepsize = 1;
      while (stepsize >= minstep)
	{
	  newA = AB[0] + stepsize * dA;
	  newB = AB[1] + stepsize * dB;
	  newf = 0.0;
	  for (i = 0; i < svm_prob.l; i++)
	    {
	      fApB = deci_array[i] * newA + newB;
	      if (fApB >= 0)
		newf += t[i] * fApB + log(1 + exp(-fApB));
	      else
		newf += (t[i] - 1) * fApB + log(1 + exp(fApB));
	    }
	  // Check whether sufficient decrease is satisfied
	  if (newf < fval + 0.0001 * stepsize * gd)
	    {
	      AB[0] = newA;
	      AB[1] = newB;
	      fval = newf;
	      break;
	    }
	  else
	    stepsize /= 2.0;
	}
      if (stepsize < minstep)
	{
	  if(user_trace)
	    fprintf(stderr, "Line search fails in probability estimates\n");
	  break;
	}
    }
  if (j >= maxiter)
    if(user_trace)
      fprintf(stderr,
	      "Reaching maximal iterations in  probability estimates\n");
  free(t);
}

static double sigmoid_predict(double decision_value, double A, double B)
{
  double fApB = decision_value * A + B;
  if (fApB >= 0)
    {
      return exp(-fApB) / (1.0 + exp(-fApB));
    }
  else
    return 1.0 / (1 + exp(fApB)) ;
}


int crm_expr_svm_learn(CSL_CELL *csl, ARGPARSE_BLOCK *apb,
		       char *txtptr, long txtstart, long txtlen)
{
  long i, j, k;
  char ftext[MAX_PATTERN];
  long flen;
  char file1[MAX_PATTERN];
  char file2[MAX_PATTERN];
  char file3[MAX_PATTERN];
  FILE *hashf;
  long textoffset;
  unsigned int *hashes;  //  the hashes we'll sort
  long hashcounts;
  long cflags;
  long sense;
  long microgroom;
  long unique;
  char ptext[MAX_PATTERN]; //the regrex pattern
  long plen;
  regex_t regcb;
  regmatch_t match[5];
  struct stat statbuf1;      //  for statting the hash file1
  struct stat statbuf2;      //  for statting the hash file2
  time_t start_timer;
  time_t end_timer;
  double run_time;

  i = 0;
  j = 0;
  k = 0;
  start_timer = time(NULL);

  //            set our cflags, if needed.  The defaults are
  //            "case" and "affirm", (both zero valued).
  //            and "microgroom" disabled.
  cflags = REG_EXTENDED;
  sense = +1;
  if (apb->sflags & CRM_NOCASE)
    {
      cflags = cflags | REG_ICASE;
      if (user_trace)
	fprintf (stderr, "turning on case-insensitive match of filenames\n");
    };
  if (apb->sflags & CRM_REFUTE)
    {
      sense = -sense;
      if (user_trace)
	fprintf (stderr, " refuting learning\n");
    };
  microgroom = 0;
  if (apb->sflags & CRM_MICROGROOM)
    {
      microgroom = 1;
      if (user_trace)
	fprintf (stderr, " enabling microgrooming.\n");
    };
  unique = 0;
  if (apb->sflags & CRM_UNIQUE)
    {
      unique = 1;
      if (user_trace)
	fprintf (stderr, " enabling uniqueifying features.\n");
    };

  //   Note that during a LEARN in hyperspace, we do NOT use the mmap of
  //    pre-existing memory.  We just write to the end of the file instead.
  //    malloc up the unsorted hashbucket space
  hashes = calloc (HYPERSPACE_MAX_FEATURE_COUNT, sizeof (unsigned int));
  hashcounts = 0;

  //  Extract the file names for storing svm solver.( file1.svm |
  //  file2.svm | 1vs2_solver.svm )
  crm_get_pgm_arg (ftext, MAX_PATTERN, apb->p1start, apb->p1len);
  flen = apb->p1len;
  flen = crm_nexpandvar (ftext, flen, MAX_PATTERN);

  strcpy (ptext,
	  "[[:space:]]*([[:graph:]]+)[[:space:]]*\\|[[:space:]]*([[:graph:]]+)[[:space:]]*\\|[[:space:]]*([[:graph:]]+)[[:space:]]*");
  plen = strlen(ptext);
  i = crm_regcomp (&regcb, ptext, plen, cflags);
  if ( i > 0){
    crm_regerror ( i, &regcb, tempbuf, data_window_size);
    nonfatalerror ("Regular Expression Compilation Problem:", tempbuf);
    goto regcomp_failed;
  };
  k = crm_regexec (&regcb, ftext,
		   flen, 5, match, 0, NULL);
  if( k==0 )
    {
      // get three input files.
      memmove(file1,&ftext[match[1].rm_so],(match[1].rm_eo-match[1].rm_so));
      file1[match[1].rm_eo-match[1].rm_so]='\000';
      memmove(file2,&ftext[match[2].rm_so],(match[2].rm_eo-match[2].rm_so));
      file2[match[2].rm_eo-match[2].rm_so]='\000';
      memmove(file3,&ftext[match[3].rm_so],(match[3].rm_eo-match[3].rm_so));
      file3[match[3].rm_eo-match[3].rm_so]='\000';
      if(internal_trace)
	fprintf(stderr, "file1=%s\tfile2=%s\tfile3=%s\n", file1, file2, file3);
    }
  else
    {
      i = 0;
      while(ftext[i] < 0x021) i++;
      j = i;
      while(ftext[j] >= 0x021) j++;
      ftext[j] = '\000';
      strcpy(file1, &ftext[i]);
      file2[0] = '\000';
      file3[0] = '\000';
    }
  crm_regfree (&regcb);
  //    if (|Text|>0) hide the text into the .svm file

  //     get the "this is a word" regex
  crm_get_pgm_arg (ptext, MAX_PATTERN, apb->s1start, apb->s1len);
  plen = apb->s1len;
  plen = crm_nexpandvar (ptext, plen, MAX_PATTERN);

  if (txtlen > 0)
    {
      // hack: assume stride 1
      (void)crm_vector_tokenize_selector(apb,
					 txtptr, txtstart, txtlen,
					 ptext, plen,
					 NULL, 0, 0,
					 hashes, HYPERSPACE_MAX_FEATURE_COUNT,
					 &hashcounts,
					 &textoffset);
      // ??? error?

      //   Now sort the hashes array.
      //
      qsort (hashes, hashcounts, sizeof (unsigned int), &hash_compare);

      if (user_trace)
	{
	  fprintf(stderr,"sorted hashes:\n");
	  for(i=0;i<hashcounts;i++)
	    {
	      fprintf(stderr, "hashes[%ld]=%ud\n",i,hashes[i]);
	    }
	  fprintf (stderr, "Total hashes generated: %ld\n", hashcounts);
	}

      //   And uniqueify the hashes array
      //
      if (unique)
	{
	  i = 0;
	  for (j = 1; j < hashcounts; j++)
	    if (hashes[j] != hashes[i])
	      hashes[++i] = hashes[j];
	  if (hashcounts > 0)
	    hashcounts = i + 1;

	  if (user_trace)
	    fprintf (stderr, "Unique hashes generated: %ld\n", hashcounts);
	};

      //mark the end of a feature vector
      hashes[hashcounts] = 0;

      if(hashcounts > 0 && sense > 0)
	{
	  //append the hashed text to file1

	  //  Because there are probably retained hashes of the
	  //  file, we need to force an unmap-by-name which will allow a remap
	  //  with the new file length later on.
	  crm_force_munmap_filename (file1);
	  if (user_trace)
	    fprintf (stderr, "Opening a svm file %s for append.\n", file1);

	  if ((hashf = fopen ( file1 , "ab+")) == NULL)
	    {
	      nonfatalerror("Sorry, couldn't open the .svm file", "");
	      return (0);
	    }
	  if (user_trace)
	    fprintf (stderr, "Writing to a svm file %s\n", file1);
	  //    and write the sorted hashes out.
	  dontcare = fwrite (hashes, 1,
		  sizeof (unsigned int) * (hashcounts + 1),
		  hashf);
	  fclose (hashf);
	}
      /////////////////////////////////////////////////////////////////////
      //     Start refuting........
      //     What we have to do here is find the set of hashes that matches
      //     the input most closely - and then remove it.
      //
      //     For this, we want the single closest set of hashes.  That
      //     implies highest radiance, so we use the same bit of code
      //     we use down in classification.  We also keep start and
      //     end of the "best match" segment.
      ////////////////////////////////////////////////////////////////////
      if (hashcounts > 0 && sense < 0)
	{
	  long beststart, bestend;
	  long thisstart, thislen, thisend;
	  double bestrad;
	  long wrapup;
	  double kandu, unotk, knotu, dist, radiance;
	  long k, u;
	  long file_hashlens;
	  unsigned int *file_hashes;

	  //   Get the file mmapped so we can find the closest match
	  //

	  struct stat statbuf;      //  for statting the hash file

	  //             stat the file to get it's length
	  k = stat (file1, &statbuf);

	  //              does the file really exist?
	  if (k != 0)
	    {
	      nonfatalerror ("Refuting from nonexistent data cannot be done!"
			     " More specifically, this data file doesn't exist: ",
			     file1);
	      return (0);
	    }
	  else
	    {
	      file_hashlens = statbuf.st_size;
	      file_hashes = (unsigned int *)
		crm_mmap_file (file1,
			       0, file_hashlens,
			       PROT_READ | PROT_WRITE,
			       MAP_SHARED,
			       NULL);
	      file_hashlens = file_hashlens / sizeof (unsigned int );
	    };
	  wrapup = 0;

	  k = u = 0;
	  beststart = bestend = 0;
	  bestrad = 0.0;
	  while (k < file_hashlens)
	    {
	      long cmp;
	      //   Except on the first iteration, we're looking one cell
	      //   past the 0x0 start marker.
	      kandu = 0;
	      knotu = unotk = 10 ;
	      u = 0;
	      thisstart = k;
	      if (internal_trace)
		fprintf (stderr,
			 "At featstart, looking at %u (next bucket value is %u)\n",
			 file_hashes[thisstart],
			 file_hashes[thisstart+1]);
	      while (wrapup == 0)
		{
		  //    it's an in-class feature.
		  cmp = hash_compare (&hashes[u], &file_hashes[k]);
		  if (cmp < 0)
		    {              // unknown less, step u forward
		      //   increment on u, because maybe k will match next time
		      unotk++;
		      u++;
		    }
		  if (cmp == 0)  // features matched.
		    //   These aren't the features you're looking for.
		    //   Move along, move along....
		    {
		      u++;
		      k++;
		      kandu++;
		    };
		  if (cmp > 0)  // unknown is greater, step k forward
		    {
		      //  increment on k, because maybe u will match next time.
		      knotu++;
		      k++;
		    };
		  //   End of the U's?  If so, skip k to the end marker
		  //    and finish.
		  if ( u >= hashcounts )
		    {
		      while ( k < file_hashlens
			      && file_hashes[k] != 0)
			{
			  k++;
			  knotu++;
			};
		    };
		  //   End of the K's?  If so, skip U to the end marker
		  if ( k >= file_hashlens - 1
		       || file_hashes[k] == 0  )  //  end of doc features
		    {
		      unotk += hashcounts - u;
		    };

		  //  end of the U's or end of the K's?  If so, end document.
		  if (u >= hashcounts
		      || k >= file_hashlens - 1
		      || file_hashes[k] == 0)  // this sets end-of-document
		    {
		      wrapup = 1;
		      k++;
		    };
		};
	      //  Now the per-document wrapup...
	      wrapup = 0;                     // reset wrapup for next file

	      //   drop our markers for this particular document.  We are now
	      //   looking at the next 0 (or end of file).
	      thisend = k - 1;
	      thislen = thisend - thisstart + 1;
	      if (internal_trace)
		fprintf (stderr,
			 "At featend, looking at %u (next bucket value is %u)\n",
			 file_hashes[thisend],
			 file_hashes[thisend+1]);

	      //  end of a document- process accumulations

	      //    Proper pythagorean (Euclidean) distance - best in
	      //   SpamConf 2006 paper
	      dist = sqrtf (unotk + knotu) ;

	      // PREV RELEASE VER --> radiance = 1.0 / ((dist * dist )+ 1.0);
	      //
	      //  This formula was the best found in the MIT `SC 2006 paper.
	      radiance = 1.0 / (( dist * dist) + .000001);
	      radiance = radiance * kandu;
	      radiance = radiance * kandu;

	      if (user_trace)
		fprintf (stderr, "Feature Radiance %f at %ld to %ld\n",
			 radiance, thisstart, thisend);
	      if (radiance >= bestrad)
		{
		  beststart = thisstart;
		  bestend = thisend;
		  bestrad = radiance;
		}
	    };
	  //  end of the per-document stuff - now chop out the part of the
	  //  file between beststart and bestend.

	  if (user_trace)
	    fprintf (stderr,
		     "Deleting feature from %ld to %ld (rad %f) of file %s\n",
		     beststart, bestend, bestrad, file1);

	  //   Deletion time - move the remaining stuff in the file
	  //   up to fill the hole, then msync the file, munmap it, and
	  //   then truncate it to the new, correct length.
	  {
	    long newhashlen, newhashlenbytes;
	    newhashlen = file_hashlens - (bestend + 1 - beststart);
	    newhashlenbytes=newhashlen * sizeof (unsigned int);
	    memmove (&file_hashes[beststart],
		     &file_hashes[bestend+1],
		     sizeof (unsigned int) * (file_hashlens - bestend) );
	    crm_force_munmap_filename (file1);

	    if (internal_trace)
	      fprintf (stderr, "Truncating file to %ld cells ( %ld bytes)\n",
		       newhashlen,
		       newhashlenbytes);
	    k = truncate (file1,
			  newhashlenbytes);
	  }
	};
    }

  //  let go of the hashes.
  free (hashes);
  if ( sense < 0 )
    {
      // finish refuting....
      return (0);
    }

  //           extract parameters for svm
  crm_get_pgm_arg(ptext, MAX_PATTERN, apb->s2start, apb->s2len);
  plen = apb->s2len;
  plen = crm_nexpandvar (ptext, plen, MAX_PATTERN);
  if(plen)
    {
      sscanf(ptext,
	     "%d %d %lf %lf %lf %lf %lf %d",
	     &(param.svm_type),
	     &(param.kernel_type),
	     &(param.cache_size),
	     &(param.eps),
	     &(param.C),
	     &(param.nu),
	     &(param.max_run_time) ,
	     &(param.shrinking));
    }
  else
    {
      //set default parameters for SVM
      param.svm_type = C_SVC;
      param.kernel_type = LINEAR;
      param.cache_size = 100;    // MB
      param.eps = 1e-3;
      param.C = 1;
      param.max_run_time = 1;
      param.shrinking = 1;       //  not available right now
    }

  //if svm_type is ONE_CLASS, then do one class svm
  if(param.svm_type == ONE_CLASS)
    {
      long file1_hashlens;
      unsigned int *file1_hashes;
      unsigned int **x = NULL;
      int *y = NULL;
      int k1;
      k1 = stat (file1, &statbuf1);
      file1_hashlens = statbuf1.st_size;
      file1_hashes = (unsigned int *)
	crm_mmap_file (file1,
		       0, file1_hashlens,
		       PROT_READ | PROT_WRITE,
		       MAP_SHARED,
		       NULL);
      file1_hashlens = file1_hashlens / sizeof (unsigned int );
      //find out how many documents in file1
      for(i = 0;i< file1_hashlens;i++){
	if(internal_trace)
	  fprintf (stderr,
		   "\nThe %ldth hash value in file1 is %ud",
		   i, file1_hashes[i]);
	if(file1_hashes[i] == 0){
	  k1 ++;
	}
      }
      if(internal_trace)
	fprintf (stderr,
		 "\nThe total number of documents in file1 is %d\n",
		 k1);

      //initialize the svm_prob.x, svm_prob.y
      svm_prob.l = k1;
      y = calloc(svm_prob.l , sizeof(y[0]));
      x = calloc(svm_prob.l , sizeof(x[0]));
      for(i = 0; i < k1; i++)
	y[i] = 1;
      svm_prob.y = y;
      x[0] = &(file1_hashes[0]);
      k = 1;
      for(i = 1;i< file1_hashlens - 1;i++)
	{
	  if(file1_hashes[i] == 0)
	    {
	      x[k++] = &(file1_hashes[i+1]);
	    }
	}
      svm_prob.x = x;
      if(internal_trace)
	{
	  for(i = 0;i< k;i++)
	    {
	      fprintf(stderr, "\nx[%ld]=%ud\n",i,x[i][1]);
	    }
	}
      Q_init();
      solve(); //result is in solver

      //free cache
      cache_free(&svmcache);
    }


  //if file2 is not empty, open file1 and file2, calculate hyperplane,
  //and write the solution to file3
  if(file2[0] != '\000')
    {
      long file1_hashlens;
      unsigned int *file1_hashes;
      long file2_hashlens;
      unsigned int *file2_hashes;
      int k1, k2;
      k1 = stat (file1, &statbuf1);
      k2 = stat (file2, &statbuf2);
      if (k1 != 0)
	{
	  nonfatalerror ("Sorry, there has no enough data to calculate the hyperplane"
			 "", file1);
	  return (0);
	}
      else if(k2 != 0)
	{
	   nonfatalerror ("Sorry, there has no enough data to calculate the hyperplane"
			 "", file2);
	  return (0);
	}
      else
	{
	  k1 = 0;
	  k2 = 0;
	  file1_hashlens = statbuf1.st_size;
	  file1_hashes = (unsigned int *)
	    crm_mmap_file (file1,
			   0, file1_hashlens,
			   PROT_READ | PROT_WRITE,
			   MAP_SHARED,
			   NULL);
	  file1_hashlens = file1_hashlens / sizeof (unsigned int );
	  file2_hashlens = statbuf2.st_size;
	  file2_hashes = (unsigned int *)
	    crm_mmap_file (file2,
			   0, file2_hashlens,
			   PROT_READ | PROT_WRITE,
			   MAP_SHARED,
			   NULL);
	  file2_hashlens = file2_hashlens / sizeof (unsigned int );
	  for(i = 0;i< file1_hashlens;i++)
	    {
	      if( internal_trace)
		fprintf(stderr,
			"\nThe %ldth hash value in file1 is %ud",
			i, file1_hashes[i]);
	      if(file1_hashes[i] == 0)
		{
		  k1 ++;
		}
	    }
	  if(internal_trace)
	    fprintf (stderr,
		     "\nThe total number of documents in file1 is %d\n", k1);

	  for(i = 0;i< file2_hashlens;i++)
	    {
	      if(internal_trace)
		fprintf (stderr,
			 "\nThe %ldth hash value in file2 is %ud",
			 i, file2_hashes[i]);
	      if(file2_hashes[i] == 0)
		{
		  k2 ++;
		}
	    }
	  if(internal_trace)
	    fprintf (stderr,
		     "\nThe total number of documents in file2 is %d\n", k2);

	  if((k1 > 0) && (k2 >0))
	    {
	      //initialize the svm_prob.x, svm_prob.y
	      int *y = NULL;
	      double b;
	      double *deci_array = NULL;
	      double AB[2];
	      unsigned int **x = NULL;

	      svm_prob.l = k1 + k2;
	      y = calloc(svm_prob.l , sizeof(y[0]));
	      x = calloc(svm_prob.l , sizeof(x[0]));
	      for(i = 0; i < k1; i++)
	        y[i] = 1;
	      for(i = k1; i < svm_prob.l; i++)
	        y[i] = -1;
	      svm_prob.y = y;
	      x[0] = &(file1_hashes[0]);
	      k = 1;
	      for(i = 1;i< file1_hashlens - 1;i++)
		{
		  if(file1_hashes[i] == 0)
		    {
		      x[k++] = &(file1_hashes[i+1]);
		    }
		}
	      x[k++] = &(file2_hashes[0]);
	      for(i = 1;i< file2_hashlens - 1;i++)
		{
		  if(file2_hashes[i] == 0)
		    {
		      x[k++] = &(file2_hashes[i+1]);
		    }
		}
	      svm_prob.x = x;
	      if(internal_trace)
		{
		  for(i = 0;i< k;i++){
		    fprintf(stderr, "\nx[%ld]=%ud\n",i,x[i][1]);
		  }
		}
	      Q_init();
	      solve(); //result is in solver
	      b = calc_b();
	      deci_array = (double*) malloc(svm_prob.l*sizeof(double));

	      //compute decision values for all training documents
	      for(i = 0; i < svm_prob.l; i++){
		deci_array[i] = calc_decision(svm_prob.x[i], solver.alpha, b);
	      }
	      if (internal_trace)
		fprintf(stderr, "done********\n");
	      calc_AB(AB,deci_array, k1,k2);
	      end_timer = time(NULL);
	      run_time = difftime(end_timer, start_timer);
	      if(user_trace)
		fprintf(stderr, "run_time =  %lf seconds\n", run_time);

	      //IF MICROGROOMING IS ENABLED, WE'LL GET RID OF LESS THAN
	      //10% DOCUMENTS THAT ARE FAR AWAY FROM THE HYPERPLANE
	      //(WITH HIGH ABSOLUTE DECISION VALUE). HERE HAVE TWO
	      //PARAMETERS YOU CAN CONTROL, ONE IS DELETE_FRACTION, THE
	      //OTHER IS THE HOW FAR AWAY FROM THE HYPERPLANE. DEFAULT
	      //SET DELETE_FRACTION=5% AND IF |DISTANCE| > 1.2 * AVERAGE
	      //DISTANCE, THEN IT IS FAR AWAY FROM THE hyperplane.
	      //
	      if(microgroom && (run_time > param.max_run_time))
		{
		  float distance_fraction = 1.2;
		  int *id_desc;
		  //int *id_asc;
		  double average1 = 0.0, average2 = 0.0;
		  float delete_fraction;
		  int delete_num1 = 0, delete_num2 = 0;
		  id_desc = (int*) malloc(svm_prob.l * sizeof(int));
		  //id_asc = (int*) malloc(svm_prob.l * sizeof(int));
		  if(user_trace)
		    fprintf(stderr, "\nStart microgrooming......\n");
		  solver.deci_array = deci_array;
		  // set an upper bound of delete fraction
		  delete_fraction = pow(param.max_run_time/run_time,
					      1.0/3.0);
		  for (i = 0; i < svm_prob.l; i++)
		    {
		      id_desc[i] = i;
		      if(i < k1)
			{
			  average1 += solver.deci_array[i];
			}
		      else
			average2 += solver.deci_array[i];
		    }
		  average1 /= k1;
		  average2 /= k2;
		  qsort (id_desc, svm_prob.l, sizeof (int),
			 &descend_int_decision_compare );
		  if(user_trace)
		    fprintf(stderr,
			   "average1=%lf\taverage2=%lf\n", average1, average2);

		  //  if decision[i] > 1.5 * average decision value, then
		  //  get rid of it.
		  i = 0;
		  j = svm_prob.l - 1;
		  while (((delete_num1 + delete_num2)
			  < floor(delete_fraction * svm_prob.l))
			 && (i < svm_prob.l) && (j >= 0))
		    {
		      if((k1 - delete_num1) > (k2 - delete_num2))
			{
			  //delete the farest document from the first class
			  if (svm_prob.y[id_desc[i]] == 1
			      && fabs(solver.deci_array[id_desc[i]])
			      > distance_fraction * fabs(average1))
			    {
			      if(user_trace)
				fprintf(stderr,
				    "delete document %d!decision value=%lf\n",
				    id_desc[i],solver.deci_array[id_desc[i]]);
			      id_desc[i] = -1;
			      delete_num1 ++;
			    }
			  i++;
			}
		      else if((k1 - delete_num1) < (k2 - delete_num2))
			{
			  //delete the farest document from the second class
			  if(svm_prob.y[id_desc[j]] == -1
			     && fabs(solver.deci_array[id_desc[j]]) > distance_fraction * fabs(average2))
			    {
			      if(user_trace)
				fprintf(stderr,
				    "delete document %d!decision value=%lf\n",
				     id_desc[j],solver.deci_array[id_desc[j]]);
			      id_desc[j] = -1;
			      delete_num2 ++;
			    }
			  j--;
			}
		      else
			{
			  //delete the farest document from both classes
			  if(svm_prob.y[id_desc[i]] == 1
		       && fabs(solver.deci_array[id_desc[i]]) > distance_fraction * fabs(average1))
			    {
			      if(user_trace)
				fprintf(stderr,
				    "delete document %d!decision value=%lf\n",
				     id_desc[i],solver.deci_array[id_desc[i]]);
			      id_desc[i] = -1;
			      delete_num1 ++;
			    }
			  i++;
			  if(svm_prob.y[id_desc[j]] == -1
                            && fabs(solver.deci_array[id_desc[j]]) > distance_fraction * fabs(average2))
			    {
			      if(user_trace)
				fprintf(stderr,
				    "delete document %d!decision value=%lf\n",
				    id_desc[j],solver.deci_array[id_desc[j]]);
			      id_desc[j] = -1;
			      delete_num2 ++;
			    }
			  j--;
			}
		    }
		  if(user_trace)
		    fprintf(stderr,
			    "delete_num1 = %d\t delete_num2 = %d\n",
			    delete_num1, delete_num2);

		  if(delete_num1 != 0 || delete_num2 != 0)
		    {
		      unsigned int *new_x[k1 + k2 - delete_num1 - delete_num2];
		      qsort (id_desc, svm_prob.l, sizeof (int), &int_compare );
		      //now start deleting documents and write the
		      //remain documents to file1, this is unrecoverable.
		      j = 0;
		      for(i = 0; i < svm_prob.l; i++)
			{
			  if((id_desc[i] != -1) && (id_desc[i] < k1))
			    {
			      int temp_i;
			      int temp_count = 0;
			      //newalpha[j] = solver.alpha[id_desc[i]];
			      svm_prob.y[j] = svm_prob.y[id_desc[i]];
			      while(svm_prob.x[id_desc[i]][temp_count]!=0)
				temp_count ++;
			      new_x[j] = (unsigned int *)
				malloc((temp_count + 1) * sizeof(unsigned int));
			      for(temp_i = 0; temp_i<temp_count; temp_i++)
				new_x[j][temp_i] = svm_prob.x[id_desc[i]][temp_i];
			      new_x[j][temp_count] = 0 ;
			      j++;
			    }
			  else if((id_desc[i] != -1) && (id_desc[i] >= k1))
			    {
			      int temp_count = 0;
			      int temp_i;
			      //newalpha[j] = solver.alpha[id_desc[i]];
			      svm_prob.y[j] = svm_prob.y[id_desc[i]];
			      while(svm_prob.x[id_desc[i]][temp_count] != 0)
				temp_count ++;
			      new_x[j] = (unsigned int *)
				malloc((temp_count + 1) * sizeof(unsigned int));
			      for(temp_i = 0; temp_i<temp_count; temp_i++)
				new_x[j][temp_i] = svm_prob.x[id_desc[i]][temp_i];
			      new_x[j][temp_count] = 0 ;
			      j++;
			    }
			}
		      svm_prob.l = k1 + k2 - delete_num1 - delete_num2;
		      svm_prob.x = new_x;
		      if(delete_num1 != 0)
			{
			  crm_force_munmap_filename (file1);
			  if (user_trace)
			    fprintf (stderr,
				     "Opening a svm file %s for rewriting.\n",
				     file1);
			  hashf = fopen ( file1 , "wb+");
			  if (user_trace)
			    fprintf (stderr,
				     "Writing to a svm file %s\n",
				     file1);
			  for(i = 0; i < k1 - delete_num1; i++)
			    {
			      int temp_count = 0;
			      while(svm_prob.x[i][temp_count] != 0)
				{
				  temp_count ++;
				}
			      dontcare = fwrite (svm_prob.x[i], 1,
				      sizeof (unsigned int) * (temp_count+1),
				      hashf);
			    }
			  fclose (hashf);
			}
		      if(delete_num2 != 0)
			{
			  crm_force_munmap_filename (file2);
			  if (user_trace)
			    fprintf (stderr,
				     "Opening a svm file %s for rewriting.\n",
				     file2);
			  hashf = fopen ( file2 , "wb+");
			  if (user_trace)
			    fprintf (stderr,
				     "Writing to a svm file %s\n", file2);
			  for(i = k1 - delete_num1; i < svm_prob.l; i++)
			    {
			      int temp_count = 0;
			      while(svm_prob.x[i][temp_count] != 0)
				temp_count ++;
			      dontcare = fwrite (svm_prob.x[i], 1,
				      sizeof (unsigned int) * (temp_count+1),
				      hashf);
			    }
			  fclose (hashf);
			}

		      k1 -= delete_num1;
		      k2 -= delete_num2;

		      //recalculate the hyperplane
		      //
		      //free cache
		      cache_free(&svmcache);
		      if(user_trace)
			fprintf(stderr, "recalculate the hyperplane!\n");
		      //recalculate the hyperplane
		      Q_init();
		      solve(); //  result is in solver
		      b = calc_b();
		      if(internal_trace)
			fprintf(stderr, "b=%lf\n",b);
		      if (user_trace)
			fprintf(stderr,
				"Finishing calculate the hyperplane\n");

		      //compute A,B for sigmoid prediction
		      deci_array = (double*) realloc(deci_array, svm_prob.l
						     * sizeof(double));
		      for(i = 0; i < svm_prob.l; i++)
			{
			  deci_array[i] = calc_decision (svm_prob.x[i],
							 solver. alpha, b);
			}
		      calc_AB(AB, deci_array, k1, k2);
		      if (internal_trace)
			fprintf(stderr,
			       "Finished calculating probability parameter\n");
		      for(i = 0; i < svm_prob.l; i++)
			{
			  free(new_x[i]);
			}
		    }
		  free(id_desc);
		}
	      free(deci_array);

	      //  write solver to file3
	      if (user_trace)
		fprintf (stderr,
		      "Opening a solution file %s for writing alpha and b.\n",
			 file3);
	      hashf = fopen ( file3 , "w+");
	      if (user_trace)
		fprintf (stderr, "Writing to a svm solution file %s\n", file3);

	      dontcare = fwrite(&k1, sizeof(int), 1, hashf);
	      dontcare = fwrite(&k2, sizeof(int), 1, hashf);
	      for (i = 0; i < svm_prob.l; i++)
		dontcare = fwrite(&(solver.alpha[i]), sizeof(double), 1, hashf);
	      dontcare = fwrite(&b, sizeof(double), 1, hashf);
	      dontcare = fwrite(&AB[0], sizeof(double), 1, hashf);
	      dontcare = fwrite(&AB[1], sizeof(double), 1, hashf);

	      fclose (hashf);

	      //free cache
	      cache_free(&svmcache);
	      free(solver.G);
	      free(DiagQ);
	      free(solver.alpha);
	      if(user_trace)
		fprintf(stderr,
	      "Finish calculating SVM hyperplane, store the solution to %s!\n",
			file3);
	    }
	  else
	    {
	      if (user_trace)
		fprintf(stderr,
	     "There hasn't enough documents to calculate a svm hyperplane!\n");
	    }
	  crm_force_munmap_filename (file1);
	  crm_force_munmap_filename (file2);
	  crm_force_munmap_filename (file3);
	}
    }//end if(file2[0])
 regcomp_failed:
  return 0;
}

int crm_expr_svm_classify(CSL_CELL *csl, ARGPARSE_BLOCK *apb,
			  char *txtptr, long txtstart, long txtlen){
  long i,j, k;
  char ftext[MAX_PATTERN];
  long flen;
  char ptext[MAX_PATTERN]; //the regrex pattern
  long plen;
  char file1[MAX_PATTERN];
  char file2[MAX_PATTERN];
  char file3[MAX_PATTERN];
  regex_t regcb;
  regmatch_t match[5];
  FILE *hashf;
  long textoffset;
  unsigned int *hashes;  //  the hashes we'll sort
  long hashcounts;
  long cflags;
  long microgroom;
  long unique;
  struct stat statbuf1;      //  for statting the hash file1
  struct stat statbuf2;      //  for statting the hash file2
  struct stat statbuf3;      //  for statting the hash file3
  double *alpha;
  double b;
  double AB[2];
  long slen;
  char svrbl[MAX_PATTERN];  //  the match statistics text buffer
  long svlen;
  //  the match statistics variable
  char stext [MAX_PATTERN+MAX_CLASSIFIERS*(MAX_FILE_NAME_LEN+100)];
  long stext_maxlen = MAX_PATTERN+MAX_CLASSIFIERS*(MAX_FILE_NAME_LEN+100);
  double decision = 0;

  long totalfeatures = 0;   //  total features
  long bestseen;
  double ptc[MAX_CLASSIFIERS]; // current running probability of this class
  long hashlens[MAX_CLASSIFIERS];
  char *hashname[MAX_CLASSIFIERS];
  long doc_num[MAX_CLASSIFIERS];

  //            extract the optional "match statistics" variable
  //
  crm_get_pgm_arg (svrbl, MAX_PATTERN, apb->p2start, apb->p2len);
  svlen = apb->p2len;
  svlen = crm_nexpandvar (svrbl, svlen, MAX_PATTERN);
  {
    long vstart, vlen;
    crm_nextword (svrbl, svlen, 0, &vstart, &vlen);
    memmove (svrbl, &svrbl[vstart], vlen);
    svlen = vlen;
    svrbl[vlen] = '\000';
  };

  //     status variable's text (used for output stats)
  //
  stext[0] = '\000';
  slen = 0;

  //            set our cflags, if needed.  The defaults are
  //            "case" and "affirm", (both zero valued).
  //            and "microgroom" disabled.
  cflags = REG_EXTENDED;
  if (apb->sflags & CRM_NOCASE)
    {
      cflags = cflags | REG_ICASE;
      if (user_trace)
	fprintf (stderr, "turning on case-insensitive match\n");
    };
  microgroom = 0;
  if (apb->sflags & CRM_MICROGROOM)
    {
      microgroom = 1;
      if (user_trace)
	fprintf (stderr, " enabling microgrooming.\n");
  };
  unique = 0;
  if (apb->sflags & CRM_UNIQUE)
    {
      unique = 1;
      if (user_trace)
	fprintf (stderr, " enabling uniqueifying features.\n");
    };

  //    malloc up the unsorted hashbucket space
  hashes = calloc (HYPERSPACE_MAX_FEATURE_COUNT, sizeof (unsigned int));
  hashcounts = 0;

  //     get the "this is a word" regex
  crm_get_pgm_arg (ptext, MAX_PATTERN, apb->s1start, apb->s1len);
  plen = apb->s1len;
  plen = crm_nexpandvar (ptext, plen, MAX_PATTERN);

  //   Now tokenize the input text
  //   We got txtptr, txtstart, and txtlen from the caller.
  //

  if (txtlen > 0)
    {
      // hack: assume stride 1
      (void)crm_vector_tokenize_selector(apb,
					 txtptr, txtstart, txtlen,
					 ptext, plen,
					 NULL, 0, 0,
					 hashes, HYPERSPACE_MAX_FEATURE_COUNT,
					 &hashcounts,
					 &textoffset);
      // ??? error?

      //   Now sort the hashes array.
      //

      qsort (hashes, hashcounts, sizeof (unsigned int), &hash_compare);
      if (user_trace)
	fprintf (stderr, "Total hashes generated: %ld\n", hashcounts);

      //   And uniqueify the hashes array
      //
      totalfeatures = hashcounts;
      if (unique)
	{
	  i = 0;
	  for (j = 1; j < hashcounts; j++)
	    if (hashes[j] != hashes[i])
	      hashes[++i] = hashes[j];
	  if (hashcounts > 0)
	    hashcounts = i + 1;
	  if (user_trace)
	    fprintf (stderr, "Unique hashes generated: %ld\n", hashcounts);
	  totalfeatures = hashcounts;
	}

      //mark the end of a feature vector
      hashes[hashcounts] = 0;
    }
  else
    {
      nonfatalerror ("Sorry, but I can't classify the null string.", "");
      return 0;
    }

  if (user_trace)
    {
      fprintf(stderr,"sorted hashes:\n");
      for (i=0;i<hashcounts;i++)
	{
	  fprintf(stderr, "hashes[%ld]=%ud\n",i,hashes[i]);
	}
      fprintf (stderr, "Total hashes generated: %ld\n", hashcounts);
    }

  // extract the file names.( file1.svm | file2.svm | 1vs2_solver.svm )
  crm_get_pgm_arg (ftext, MAX_PATTERN, apb->p1start, apb->p1len);
  flen = apb->p1len;
  flen = crm_nexpandvar (ftext, flen, MAX_PATTERN);
  strcpy (ptext,
	  "[[:space:]]*([[:graph:]]+)[[:space:]]+\\|[[:space:]]+([[:graph:]]+)[[:space:]]+\\|[[:space:]]+([[:graph:]]+)[[:space:]]*");
  plen = strlen(ptext);
  i = crm_regcomp (&regcb, ptext, plen, cflags);
  if ( i > 0)
    {
      crm_regerror ( i, &regcb, tempbuf, data_window_size);
      nonfatalerror ("Regular Expression Compilation Problem:", tempbuf);
      goto regcomp_failed;
    };
  k = crm_regexec (&regcb, ftext,
		   flen, 5, match, 0, NULL);
  if( k==0 )
    {
      long file1_hashlens;
      unsigned int *file1_hashes;
      long file2_hashlens;
      unsigned int *file2_hashes;
      int k1, k2, k3;
      //get three input files.
      memmove(file1,&ftext[match[1].rm_so],(match[1].rm_eo-match[1].rm_so));
      file1[match[1].rm_eo-match[1].rm_so]='\000';
      memmove(file2,&ftext[match[2].rm_so],(match[2].rm_eo-match[2].rm_so));
      file2[match[2].rm_eo-match[2].rm_so]='\000';
      memmove(file3,&ftext[match[3].rm_so],(match[3].rm_eo-match[3].rm_so));
      file3[match[3].rm_eo-match[3].rm_so]='\000';
      if(internal_trace)
	fprintf(stderr, "file1=%s\tfile2=%s\tfile3=%s\n", file1, file2, file3);

      // open all files,
      // first check whether file3 is the current version solution.
      k1 = stat (file1, &statbuf1);
      k2 = stat (file2, &statbuf2);
      k3 = stat (file3, &statbuf3);

      if (k1 != 0)
	{
	  nonfatalerror ("Refuting from nonexistent data cannot be done!"
			 " More specifically, this data file doesn't exist: ",
			 file1);
	  return (0);
	}
      else if(k2 != 0)
	{
      nonfatalerror ("Refuting from nonexistent data cannot be done!"
		     " More specifically, this data file doesn't exist: ",
		     file2);
      return (0);
	}
      else
	{
	  int temp_k1 = 0, temp_k2 = 0;
	  int *y = NULL;
	  unsigned int **x = NULL;

	  k1 = 0;
	  k2 = 0;

	  file1_hashlens = statbuf1.st_size;
	  crm_force_munmap_filename (file1);
	  crm_force_munmap_filename (file2);

	  file1_hashes = (unsigned int *)
	    crm_mmap_file (file1,
			   0, file1_hashlens,
			   PROT_READ | PROT_WRITE,
			   MAP_SHARED,
			   NULL);
	  file1_hashlens = file1_hashlens / sizeof (unsigned int );

	  hashlens[0] = file1_hashlens;
	  hashname[0] = (char *) malloc (strlen(file1)+10);
	  if (!hashname[0])
	    untrappableerror( "Couldn't malloc hashname[0]\n",
			  "We need that part later, so we're stuck.  Sorry.");
	  strcpy(hashname[0],file1);


	  file2_hashlens = statbuf2.st_size;
	  file2_hashes = (unsigned int *)
	    crm_mmap_file (file2,
			   0, file2_hashlens,
			   PROT_READ | PROT_WRITE,
			   MAP_SHARED,
			   NULL);
	  file2_hashlens = file2_hashlens / sizeof (unsigned int );
	  hashlens[1] = file2_hashlens;
	  hashname[1] = (char *) malloc (strlen(file2)+10);
	  if (!hashname[1])
	    untrappableerror("Couldn't malloc hashname[1]\n",
			  "We need that part later, so we're stuck.  Sorry.");
	  strcpy(hashname[1],file2);

	  //find out how many documents in file1 and file2 separately
	  for(i = 0;i< file1_hashlens;i++)
	    {
	      if(internal_trace)
		fprintf (stderr,
			 "\nThe %ldth hash value in file1 is %ud",
			 i, file1_hashes[i]);
	      if(file1_hashes[i] == 0)
		{
		  k1 ++;
		}
	    }
	  if(internal_trace)
	    fprintf (stderr,
		     "\nThe total number of documents in file1 is %d\n", k1);

	  for(i = 0;i< file2_hashlens;i++)
	    {
	      if(internal_trace)
		fprintf (stderr,
			 "\nThe %ldth hash value in file2 is %ud",
			 i, file2_hashes[i]);
	      if(file2_hashes[i] == 0)
		{
		  k2 ++;
		}
	    }

	  if(internal_trace)
	    fprintf (stderr,
		     "\nThe total number of documents in file2 is %d\n", k2);
	  hashf = fopen ( file3 , "r+");
	  if(k3 == 0)
	    {
	      //hashf = fopen ( file3 , "r+");
	      dontcare = fread(&temp_k1, sizeof(int), 1, hashf);
	      dontcare = fread(&temp_k2, sizeof(int), 1, hashf);
	      //fscanf(hashf,"%d\t%d", &temp_k1, &temp_k2);
	      if (internal_trace)
		fprintf(stderr, "temp_k1=%d\ttemp_k2=%d\n",temp_k1,temp_k2);
	    }
	  doc_num[0] = k1;
	  doc_num[1] = k2;
	  //assign svm_prob.x, svm_prob.y
	  svm_prob.l = k1 + k2;
	  y = calloc(svm_prob.l , sizeof(y[0]));
	  x = calloc(svm_prob.l , sizeof(x[0]));
	  for(i = 0; i < k1; i++)
	    y[i] = 1;
	  for(i = k1; i < svm_prob.l; i++)
	    y[i] = -1;
	  svm_prob.y = y;
	  x[0] = &(file1_hashes[0]);
	  k = 1;
	  for(i = 1;i< file1_hashlens - 1;i++)
	    {
	      if(file1_hashes[i] == 0)
		{
		  x[k++] = &(file1_hashes[i+1]);
		}
	    }
	  x[k++] = &(file2_hashes[0]);
	  for(i = 1;i< file2_hashlens - 1;i++)
	    {
	      if(file2_hashes[i] == 0)
	 	{
		  x[k++] = &(file2_hashes[i+1]);
		}
	    }
	  svm_prob.x = x;

	  alpha = (double *)malloc(svm_prob.l * sizeof(double));

	  if((k3 != 0) || (temp_k1 != k1) || (temp_k2 != k2))
	    {
	      if(user_trace)
		fprintf(stderr,
			"temp_k1=%d\ttemp_k2=%d\tSVM solution file is not up-to-date! we'll recalculate it!\n",
			temp_k1, temp_k2);
	      //recalculate the svm solution
	      if((k1 > 0) && (k2 >0))
		{
		  double *deci_array = NULL;
		  //           extract parameters for svm
		  crm_get_pgm_arg(ptext, MAX_PATTERN,apb->s2start, apb->s2len);
		  plen = apb->s2len;
		  plen = crm_nexpandvar (ptext, plen, MAX_PATTERN);
		  if(plen)
		    {
		      sscanf(ptext,
			     "%d %d %lf %lf %lf %lf %lf %d",&(param.svm_type),
			     &(param.kernel_type),
			     &(param.cache_size),
			     &(param.eps),
			     &(param.C),
			     &(param.nu),
			     &(param.max_run_time) ,
			     &(param.shrinking));
		    }
		  else
		    {
		      //set default parameters for SVM
		      param.svm_type = C_SVC;
		      param.kernel_type = LINEAR;
		      param.cache_size = 1;//MB
		      param.eps = 1e-3;
		      param.C = 1;
		      param.max_run_time = 1; //second
		      param.shrinking = 1;//not available right now
		    }
		  if(internal_trace)
		    {
		      for(i = 0;i< k;i++)
			{
			  fprintf(stderr,
			    "\nx[%ld]=%ud\n",i,svm_prob.x[i][1]);
			};
		    };
		  Q_init();
		  solve(); //result is in solver
		  b = calc_b();
		  if(internal_trace)
		    {
		      fprintf(stderr, "b=%lf\n",b);
		    }
		  for(i = 0; i < svm_prob.l; i++)
		    alpha[i] = solver.alpha[i];

		  //compute A,B for sigmoid prediction
		  deci_array = (double*) malloc(svm_prob.l * sizeof(double));
		  for(i = 0; i < svm_prob.l; i++)
		    {
		      deci_array[i] = calc_decision(svm_prob.x[i], alpha, b);
		    }
		  calc_AB(AB, deci_array, k1, k2);
		  //free cache
		  cache_free(&svmcache);
		  free(deci_array);
		  free(solver.G);
		  free(solver.alpha);
		  free(DiagQ);
		  if(user_trace)
		    fprintf(stderr,
			    "Recalculation of svm hyperplane is finished!\n");
		}
	      else
		{
		  if(user_trace)
		    fprintf(stderr,
			    "There hasn't enough documents to recalculate a svm hyperplane!\n");
		  return (0);
		}
	    }
	  else
	    {
	      if(internal_trace)
		{
		  for(i=0;i<svm_prob.l;i++)
		    {
		      j = 0;
		      do
			{
			  fprintf (stderr,
				   "x[%ld][%ld]=%ud\n",
				   i,j,svm_prob.x[i][j]);
			}
		      while(svm_prob.x[i][j++]!=0);
		    }
		}
	      for(i = 0; i<svm_prob.l; i++)
		{
		  dontcare = fread(&alpha[i], sizeof(double), 1, hashf);
		}
	      dontcare = fread(&b, sizeof(double), 1, hashf);
	      dontcare = fread(&AB[0], sizeof(double), 1, hashf);
	      dontcare = fread(&AB[1], sizeof(double), 1, hashf);
	      fclose(hashf);
	    }
	  decision = calc_decision(hashes,alpha,b);
	  decision = sigmoid_predict(decision, AB[0], AB[1]);
	  free(alpha);
	  crm_force_munmap_filename (file1);
	  crm_force_munmap_filename (file2);
	  crm_force_munmap_filename (file3);
	}   //end (k1==0 && k2 ==0)
    } //end (k==0)
  else
    {
      nonfatalerror (
	   "You need to input (file1.svm | file2.svm | f1vsf2.svmhyp)\n", "");
      return (0);
  };
  free(hashes);


  if(svlen > 0)
    {
    char buf [4096];
    double pr;
    char fname[MAX_FILE_NAME_LEN];
    buf [0] = '\000';

    //   put in standard CRM114 result standard header:
    ptc[0] = decision;
    ptc[1] = 1 - decision;
    if(decision >= 0.5)
      {
	pr = 10 *(log10 (decision + 1e-300) - log10 (1.0 - decision +1e-300 ));
	sprintf(buf,
		"CLASSIFY succeeds; success probability: %6.4f  pR: %6.4f\n",
		decision, pr);
	bestseen = 0;
      }
    else
      {
	pr = 10 *(log10 (decision + 1e-300) - log10 (1.0 - decision +1e-300 ));
	sprintf(buf,
		"CLASSIFY fails; success probability: %6.4f  pR: %6.4f\n",
		decision, pr);
	bestseen = 1;
      }
    if (strlen (stext) + strlen(buf) <= stext_maxlen)
      strcat (stext, buf);

    //   Second line of the status report is the "best match" line:
    //
    if(bestseen)
      strcpy(fname, file2);
    else
      strcpy(fname, file1);
    sprintf (buf, "Best match to file #%ld (%s) "	\
	     "prob: %6.4f  pR: %6.4f  \n",
	     bestseen,
	     fname,
	     ptc[bestseen],
	     10 * (log10 (ptc[bestseen] + 1e-300) - log10 (1.0 - ptc[bestseen] +1e-300 )));
    if (strlen (stext) + strlen(buf) <= stext_maxlen)
      strcat (stext, buf);

    sprintf (buf, "Total features in input file: %ld\n", totalfeatures);
    if (strlen (stext) + strlen(buf) <= stext_maxlen)
      strcat (stext, buf);
    for(k = 0; k < 2; k++)
      {
	sprintf (buf,
		 "#%ld (%s):"						\
		 " documents: %ld, features: %ld,  prob: %3.2e, pR: %6.2f \n",
		 k,
		 hashname[k],
		 doc_num[k],
		 hashlens[k],
		 ptc[k],
		 10*(log10 (ptc[k] + 1e-300) - log10 (1.0 - ptc[k] + 1e-300)));

	if (strlen(stext)+strlen(buf) <= stext_maxlen)
	  strcat (stext, buf);
      }

    for(k = 0; k < 2; k++)
      {
	free(hashname[k]);
      }

    //   finally, save the status output
    //
    crm_destructive_alter_nvariable (svrbl, svlen,
				     stext, strlen (stext));
    }

  //    Return with the correct status, so an actual FAIL or not can occur.
  if (decision >= 0.5 )
    {
      //   all done... if we got here, we should just continue execution
      if (user_trace)
        fprintf (stderr, "CLASSIFY was a SUCCESS, continuing execution.\n");
    }
  else
    {
      //  Classify was a fail.  Do the skip.
      if (user_trace)
        fprintf (stderr, "CLASSIFY was a FAIL, skipping forward.\n");
      //    and do what we do for a FAIL here
      csl->cstmt = csl->mct[csl->cstmt]->fail_index - 1;
      csl->aliusstk [csl->mct[csl->cstmt]->nest_level] = -1;
      return (0);
    }
 regcomp_failed:
  return (0);

}
