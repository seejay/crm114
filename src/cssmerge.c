//  cssmerge.c - utility for merging one css file onto another
//  Copyright 2001-2007  William S. Yerazunis, all rights reserved.
//
//  This software is licensed to the public under the Free Software
//  Foundation's GNU GPL, version 1.0.  You may obtain a copy of the
//  GPL by visiting the Free Software Foundations web site at
//  www.fsf.org .  Other licenses may be negotiated; contact the
//  author for details.
//

//  include some standard files

#include "crm114_sysincludes.h"

//  include any local crm114 configuration file
#include "crm114_config.h"

//  include the crm114 data structures file
#include "crm114_structs.h"

//  and include the routine declarations file
#include "crm114.h"


//
//    Global variables

long user_trace = 0;

long internal_trace = 0;

long engine_exit_base = 0;  //  All internal errors will use this number or higher;
//  the user programs can use lower numbers freely.


//    the command line argc, argv
int prog_argc = 0;
char **prog_argv = NULL;






static char version[] = "1.2";


static void helptext(void)
{
  fprintf(stderr, " This is cssmerge, version %s\n", version);
  fprintf(stderr, " Copyright 2001-2007 W.S.Yerazunis.\n");
  fprintf(stderr,
          " This software is licensed under the GPL with ABSOLUTELY NO WARRANTY\n");
  fprintf(stdout, "Usage: cssmerge <out-cssfile> <in-cssfile> [-v] [-s]\n");
  fprintf(stdout, " <out-cssfile> will be created if it doesn't exist.\n");
  fprintf(stdout, " <in-cssfile> must already exist.\n");
  fprintf(stdout, "  -v           -verbose reporting\n");
  fprintf(stdout, "  -s NNNN      -new file length, if needed\n");
}



int main(int argc, char **argv)
{
  long i, k;   //  some random counters, when we need a loop
  long hfsize1, hfsize2;
  long new_outfile = 0;
  FILE *f;
  int verbose;
  long sparse_spectrum_file_length = DEFAULT_SPARSE_SPECTRUM_FILE_LENGTH;

  struct stat statbuf;                      //  filestat buffer
  FEATUREBUCKET_TYPE *h1, *h2;              //  the text of the hash file

  int opt;

  init_stdin_out_err_as_os_handles();

  //   copy argc and argv into global statics...
  prog_argc = argc;
  prog_argv = argv;

  user_trace = DEFAULT_USER_TRACE_LEVEL;
  internal_trace = DEFAULT_INTERNAL_TRACE_LEVEL;

  verbose = 0;

  // parse cmdline options
  while ((opt = getopt(argc, argv, "s:v")) != -1)
  {
    switch (opt)
    {
    case 'v':
      // do we want verbose?
      verbose = 1;
      break;

    case 's':
      //  override css file length?
      if (!optarg || 1 != sscanf(optarg, "%ld", &sparse_spectrum_file_length))
      {
        fprintf(stderr, "You must specify a numeric argument for the "
                        "'-s' commandline option (overriding new-create length).\n");
        exit(EXIT_FAILURE);
      }
      else
      {
        fprintf(stderr, "\nOverriding new-create length to %ld\n",
                sparse_spectrum_file_length);
      }
      break;

    default:
      helptext();
      exit(EXIT_SUCCESS);
      break;
    }
  }

  if (optind >= argc)
  {
    fprintf(stderr, "Error: missing css input & output file arguments.\n\n");
    helptext();
    exit(EXIT_FAILURE);
  }
  if (optind + 1 >= argc)
  {
    fprintf(stderr, "Error: missing css input file argument.\n\n");
    helptext();
    exit(EXIT_FAILURE);
  }


  //             quick check- does the file even exist?
  k = stat(argv[optind + 1], &statbuf);
  if (k != 0)
  {
    fprintf(stderr, "\nCouldn't find the input .CSS file %s", argv[optind + 1]);
    fprintf(stderr, "\nCan't continue\n");
    exit(EXIT_FAILURE);
  }
  //
  hfsize2 = statbuf.st_size;
  //         mmap the hash file into memory so we can bitwhack it
  h2 = crm_mmap_file(argv[optind + 1],
                     0,
                     hfsize2,
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED,
                     &hfsize2);
  if (h2 == MAP_FAILED)
  {
    fprintf(stderr, "\n Couldn't open file %s for reading; errno=%d(%s).\n",
            argv[optind + 1], errno, errno_descr(errno));
    exit(EXIT_FAILURE);
  }

  //            see if output file [f1] exists
  //             and stat it to get it's length
  k = stat(argv[optind], &statbuf);
  //             quick check- does the file even exist?
  if (k != 0)
  {
    //      file didn't exist... create it
    new_outfile = 1;
    fprintf(stderr, "\nCreating new output .CSS file %s\n", argv[optind]);
    f = fopen(argv[optind], "wb");
    if (!f)
    {
      untrappableerror_ex(SRC_LOC(),
                          "\n Couldn't open file %s for writing; errno=%d(%s)\n",
                          argv[optind], errno, errno_descr(errno));
    }
    else
    {
      CRM_PORTA_HEADER_INFO classifier_info = { 0 };
      void *orig_header;
      const char *user_msg = NULL;

      orig_header = crm_get_header_for_mmap_file(h2);
      if (!orig_header)
      {
        classifier_info.classifier_bits = CRM_OSBF;
      }
      else
      {
        CRM_DECODED_PORTA_HEADER_INFO h_inf;

        if (crm_decode_header(orig_header, CRM_OSBF | CRM_MARKOVIAN, TRUE, &h_inf))
        {
          fatalerror("The original .CSS file format is not supported by this utility.",
                     argv[optind + 1]);
          fclose(f);
          return -1;
        }

        classifier_info = h_inf.binary_section.classifier_info;
        user_msg = h_inf.human_readable_message;
      }

      if (0 != fwrite_crm_headerblock(f, &classifier_info, user_msg))
      {
        fatalerror("Couldn't write the header to the .CSS file named ",
                   argv[optind]);
        fclose(f);
        free((void *)user_msg);
        return -1;
      }
      free((void *)user_msg);

      //       put in  bytes of NULL

      // fputc(0, f);/* [i_a] fprintf(f, "%c", 0); will write ZERO bytes on some systems: read: NO BYTES AT ALL! */
      if (file_memset(f, 0,
                      sparse_spectrum_file_length * sizeof(FEATUREBUCKET_TYPE)))
      {
        untrappableerror_ex(SRC_LOC(),
                            "\n Couldn't write to file %s; errno=%d(%s)\n",
                            argv[optind], errno, errno_descr(errno));
      }
      fclose(f);
    }
    //    and reset the statbuf to be correct
    k = stat(argv[optind], &statbuf);
    CRM_ASSERT_EX(k == 0, "We just created/wrote to the file, stat shouldn't fail!");
  }
  //
  hfsize1 = statbuf.st_size;
  //         mmap the hash file into memory so we can bitwhack it
  h1 = crm_mmap_file(argv[optind],
                     0,
                     hfsize1,
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED,
                     &hfsize1);
  if (h1 == MAP_FAILED)
  {
    fprintf(stderr, "\n Couldn't map file %s; errno=%d(%s).\n",
            argv[optind], errno, errno_descr(errno));
    exit(EXIT_FAILURE);
  }

  //
  hfsize1 = hfsize1 / sizeof(FEATUREBUCKET_TYPE);
  fprintf(stderr, "\nOutput sparse spectra file %s has %ld bins total\n",
          argv[optind], hfsize1);

  hfsize2 = hfsize2 / sizeof(FEATUREBUCKET_TYPE);
  fprintf(stderr, "\nInput sparse spectra file %s has %ld bins total\n",
          argv[optind + 1], hfsize2);


#ifdef OSB_LEARNCOUNTS
  /*
   * The hash slots for litf and fitf are special and MUST be assigned
   * to the hash values for litf and fitf, or else classify and learn
   * will fail.  However, there is no guarantee that when iterating
   * through h2 that the litf and fitf slots will be the first slot to
   * get re-hashed into h1's litf and fitf slots, respectively (in
   * fact, if you are using cssmerge to resize a nearly full .css file,
   * it is likely that the slots will get mis-assigned).  Therefore,
   * pre-reserve them now.  Any other h2 slot that gets re-hashed to
   * these h1 slots will then increment forward to the next available
   * h1 slot, and all will be well.
   */
  if (new_outfile)
  {
    const char *litf = "Learnings in this file";
    const char *fitf = "Features in this file";
    crmhash_t litf_hash, fitf_hash;

    litf_hash = strnhash(litf, strlen(litf));
    h1[litf_hash % hfsize1].hash = litf_hash;

    fitf_hash = strnhash(fitf, strlen(fitf));
    h1[fitf_hash % hfsize1].hash = fitf_hash;
  }
#endif


  //
  //    Note we start at 1, not at 0, because 0 is the version #
  for (i = 1; i < hfsize2; i++)
  {
    crmhash_t hash;
    unsigned long key;
    unsigned long value;
    unsigned long incrs;
    //  grab that feature bucket out of h2,
    //  and add it's hashes and data to h1.
    hash = h2[i].hash;
    key  = h2[i].key;
    value = h2[i].value;

    //    If it's an empty bucket, do nothing, otherwise
    //    insert it into h1

    if (value != 0)
    {
      long hindex;

      if (verbose) fprintf(stderr, "0x%08lX:%lud ", (unsigned long)hash, value);
      //
      //  this bucket has real data!  :)
      hindex = hash % hfsize1;
      if (hindex == 0) hindex = 1;
      incrs = 0;
      while (h1[hindex].hash != 0
             && (h1[hindex].hash != hash
                 || h1[hindex].key  != key))
      {
        hindex++;
        if (hindex >= hfsize1) hindex = 1;
        //
        //      check to see if we've incremented ourself all the
        //      way around the .css file.  If so, we're full, and
        //      can hold no more features (this is unrecoverable)
        incrs++;
        if (incrs > hfsize1 - 3)
        {
          fprintf(stdout, "\n\n ****** FATAL ERROR ******\n"
                          "There are too many features to fit into a css file of this size.\n"
                          "Operation aborted at input bucket offset %lud .\n",
                  i);
          exit(EXIT_FAILURE);
        }
      }
      //
      //   OK, we either found the correct bucket, or
      //   an empty bucket.  Either is fine... we clobber hash and key,
      //   and add to value.
      h1[hindex].hash   = hash;
      h1[hindex].key    = key;
      h1[hindex].value += value;
    }
  }
  if (verbose) fprintf(stderr, "\n");
  exit(EXIT_SUCCESS);
}

