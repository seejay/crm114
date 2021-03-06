//  crm114_config.h  - Controllable Regex Mutilator base config, version X0.1
//  Copyright 2001-2009 William S. Yerazunis, all rights reserved.
//
//  This software is licensed to the public under the Free Software
//  Foundation's GNU GPL, version 2.  You may obtain a copy of the
//  GPL by visiting the Free Software Foundations web site at
//  www.fsf.org .  Other licenses may be negotiated; contact the
//  author for details.
//
///////////////////////////////////////////////////////////////////
//
//    Configuration for CRM114.  Some things here you can change
//    with relative impunity.  Other things, not so much.  Where
//    there are limiting factors noted, please obey them or you
//    may break something important.  And, of course, realize that
//    this is GPLed software with NO WARRANTY - make any changes
//    and that goes double.
//
///////////////////////////////////////////////////////////////////

#ifndef __CRM114_CONFIG_H__
#define __CRM114_CONFIG_H__

#ifndef CRM_ASSERT_IS_UNTRAPPABLE
#define CRM_ASSERT_IS_UNTRAPPABLE 1  /* untrappable by default, if config.h screwed up */
#endif

// new code, which copes with fail/trap cascades occurring within a single statement
// (See tests/chained_trap_error.crm)
#define TOLERATE_FAIL_AND_OTHER_CASCADES  1


// This will be defined in ./configure OR your own config_vanilla_UNIX_sys_defaults.h/config_Win32.h
//
//     Do you want all the classifiers?  Or just the "production
//     ready ones"?   Comment the next line out if you want everything.
//
//#define CRM_PRODUCTION_CLASSIFIERS_ONLY 1

//
//   default size of the variables hashtable (a.k.a. the VHT)
//
//   ADVICE: this should be a prime number for good performance.
//           depending on the hash function used, a 2^N number may work too.
//
#define DEFAULT_VHT_SIZE 4099 /* 4095 -- [i_a] 4095 is not a prime ;-) */

//   default limit on the control stack (for catching infinite loops,
//   not a preallocated variable)
#define DEFAULT_CSTK_LIMIT 1024

//   how many levels (pending operations) will we allow in
//   math evaluations.  We _could_ have it be unlimited, but
//   this serves as an error catcher in runaway programs.
#define DEFAULT_MATHSTK_LIMIT 1024

//   default maximum number of lines in any program file
#define DEFAULT_MAX_PGMLINES 10000

//   define maximum number of INSERTs before we think we're in an
//   infinite loop...
#define DEFAULT_MAX_INSERTS 1024

//   default size of the data window: 8 megabytes.
#define DEFAULT_DATA_WINDOW  8388608
//#define DEFAULT_DATA_WINDOW 16777216
//#define DEFAULT_DATA_WINDOW 1048576

//    mmap cacheing length - only actually write out this often.
//     set to 0 to disable mmap cacheing and release files faster.
//      However, this has a negative speed impact.
#define UNMAP_COUNT_MAX 0
//#define UNMAP_COUNT_MAX 2
//#define UNMAP_COUNT_MAX 1000

//    What's the smallest chunk we actually want to bother reclaiming
//    on the fly out of the isolated data area "tdw".  Set this to 1
//    for agressive compression; values like 100 to 10K can speed up
//    execution of things that thrash the tdw badly; set to larger
//    than the data window size to completely disable the on-the-fly
//    reclaimer.  Watch out though- values less than 1 can cause the
//    end of one variable to overlap the start of another; this causes
//    horrible problems.  FOR LATER IMPROVEMENT: Start with a
//    relatively large reclaimer value, then decrease slowly as memory
//    becomes more scarce.
#define MAX_RECLAIMER_GAP 5

//    How many regex compilations do we cache?  (this saves the time
//    to recompile regexes in a loop, but uses memory) Set to zero to
//    disable cacheing.  Note that we cache the actual regex, not the
//    source code line, so this happens *after* the regex text is var
//    expanded; two different expressions that evaluate to the same
//    actual regex will share the same cache slot, which is pretty
//    cool.
//
//    For programs that don't loop, or reuse the same regex a lot,
//    performance is slightly better with cacheing disabled.  But if you
//    do reuse the same regexes tens or hundreds of times (say, lots of
//    LIAF-loops) then cacheing can accelerate your program significantly.
//
//#define CRM_REGEX_CACHESIZE 0
//#define CRM_REGEX_CACHESIZE 10
#define CRM_REGEX_CACHESIZE 1024
//
//    and how do we want the regex cache to work?  RANDOM_ACCESS can
//    keep more things around, but is only 1 LRU deep for each slot so
//    use plenty of slots, like 256 or more.  LINEAR_SEARCH is a
//    strict LRU cache but that's slower; don't use too many slots
//    with LINEAR_SEARCH or you'll spend more time searching the cache
//    than you would have spent just recompiling the regex.
//
//    Be sure to turn on ONLY ONE of these !!!!
//
#define REGEX_CACHE_RANDOM_ACCESS
//#define REGEX_CACHE_LINEAR_SEARCH


//    How big a space in a "standard header" (which is relatively new
//    and most classifiers don't support yet) do we want to use?  Note
//    that changing this will break all previously generated statistics
//    files that use this standard header.
#define STATISTICS_FILE_NCHUNKS 1024
#define STATISTICS_FILE_IDENT_STRING_MAX 1024
#define STATISTICS_FILE_ARB_DATA 1024

//    do we use Sparse Binary Polynomial Hashing (sensitive to both
//    sequence and spacing of individual words), Token Grab Bag, or
//    Token Sequence Sensitive?  Testing against the SpamAssassin
//    "hard" database shows that SBPH, TGB, and TGB2, are somewhat
//    more accurate than TSS, and about 50% more accurate than First
//    Order Only.  However, that's for English, and other natural
//    languages may show a different statistical distribution.
//
//    Choose ONE of the following:
//          SBPH, TGB2, TGB, TSS, or ARBITRARY_WINDOW_LEN:
//
//    *** DANGER, WILL ROBINSON ***  You MUST rebuild your .css files from
//    samples of text if you change this.
//
//
//     Sparse Binary Polynomial Hashing
#define SBPH
//
//     Token Grab Bag, noaliasing
//#define TGB2
//
//     Token Grab Bag, aliasing
//#define TGB
//
//     Token Sequence Sensitive
//#define TSS
//
//     First Order Only (i.e. single words, like SpamBayes)
//    Note- if you use FOO, you must turn off weights!!
//#define FOO
//
//     Generalized format for the window length.
//
//  DO NOT SET THIS TO MORE THAN 10 WITHOUT LENGTHENING hctable
//  the classifier modules !!!!!!  "hctable" contains the pipeline
//  hashing coefficients and needs to be extended to 2 * WINDOW_LEN
//
//     Generic window length code
//#define ARBITRARY_WINDOW_LENGTH
//
#define MARKOVIAN_WINDOW_LEN 5
//
#define OSB_BAYES_WINDOW_LEN 5
//
//      DO NOT set this to more than 5 without lengthening the
//      htup1 and htup2 tables in crm_unified_bayes.c
//
#define UNIFIED_BAYES_WINDOW_LEN 5
//
//      Unified tokenization pipeline length.
//          maximum window length _ever_.
#define UNIFIED_WINDOW_LEN 32
//
//          maximum number of weight vectors to be applied to the pipeline
#define UNIFIED_VECTOR_LIMIT 256
//
//          maximum stride width for VT == maximum number of matrices to use.
#define UNIFIED_VECTOR_STRIDE 4

//    For the bayes-based matchers (OSB, OSBF, Winnow, Markovian), we need to define the max hash buffer size for the VT:
#define BAYES_MAX_FEATURE_COUNT 500000


////
//         Winnow algorithm parameters here...
//
#define OSB_WINNOW_WINDOW_LEN 5
#define OSB_WINNOW_PROMOTION 1.23
#define OSB_WINNOW_DEMOTION 0.83
//
//     Now, choose whether we want to use the "old" or the "new" local
//     probability calculation.  The "old" one works slightly better
//     for SBPH and much better for TSS, the "new" one works slightly
//     better for TGB and TGB2, and _much_ better for FOO
//
//     The current default (not necessarily optimal)
//     is Markovian SBPH, STATIC_LOCAL_PROBABILITIES,
//     LOCAL_PROB_DENOM = 16, and SUPER_MARKOV
//
//#define LOCAL_PROB_DENOM 2.0
#define LOCAL_PROB_DENOM 16.0
//#define LOCAL_PROB_DENOM 256.0
#define STATIC_LOCAL_PROBABILITIES
//#define LENGTHBASED_LOCAL_PROBABILITIES
//
//#define ENTROPIC_WEIGHTS
//#define MARKOV_WEIGHTS
#define SUPER_MARKOV_WEIGHTS
//#define BREYER_CHHABRA_SIEFKES_WEIGHTS
//#define BREYER_CHHABRA_SIEFKES_BASE7_WEIGHTS
//#define BCS_MWS_WEIGHTS
//#define BCS_EXP_WEIGHTS
//
//
//    Do we use learncount-based normalization in calculating probabilities?
#define OSB_LEARNCOUNTS
//
//    Do we want "compatibility mode" between .css files LEARNed under
//    Markovian versus those LEARNed with OSB or OSB/Unique?  Note that
//    the default is NOT compatible with Markov; the reason for this is
//    that Bill Y. screwed up his subscripts and caused an unintentional
//    yet hard to fix fork in the .css table hashing formula.  Both work,
//    both work exactly equally well, yet they are incompatible and you
//    must choose one or the other.  If you turn on OLD_MARKOV_COMPATIBILITY,
//    you can flip (mostly) back and forth between <microgroom> and
//    <osb microgroom>.  But if you've already built any OSB .css files,
//    you can't do this flip.  But if you have older OSB .css files,
//    you can't turn this on without having the data become inaccessible.
//    And there's no easy way to translate (due to the hashing obfuscation)
//    Anyway, it's better to rebuild from fresh spam anyway, so blame Bill.
//             - Bill
// #define OLD_MARKOV_COMPATIBILITY
//
//    Do we take only the maximum probability feature?
//
//#define USE_PEAK
//
//
//    Should we use stochastic microgrooming, or weight-distance microgrooming-
//    Make sure ONE of these is turned on.
//#define STOCHASTIC_AMNESIA
#define WEIGHT_DISTANCE_AMNESIA
//
//    define the default max chain length in a .css file that triggers
//    autogrooming, the rescale factor when we rescale, and how often
//    we rescale, and what chance (mask and key) for any particular
//    slot to get rescaled when a rescale is triggered for that slot chain.
//#define MICROGROOM_CHAIN_LENGTH 1024
#define MICROGROOM_CHAIN_LENGTH 256
//#define MICROGROOM_CHAIN_LENGTH 64
#define MICROGROOM_RESCALE_FACTOR .75
#define MICROGROOM_STOCHASTIC_MASK 0x0000000F
#define MICROGROOM_STOCHASTIC_KEY  0x00000001
#define MICROGROOM_STOP_AFTER 32    //  maximum number of buckets groom-zeroed

#define FEATURE_HIT_INCREMENT_SIZE 7

//    define the "block ratio" of how of a memory data window we're
//    willing to suck in from a minion process before we block on
//    sucking; the un-sucked part just waits in the minion's stdout
//    buffer (and causes the minion to block on output).  Normally a
//    factor of 2 (1/4th of the size of a full memory window, or 2
//    megabytes in the default configuraton) is sufficient.
#define SYSCALL_WINDOW_RATIO 2

//   define default internal debug level
#define DEFAULT_INTERNAL_TRACE_LEVEL 0

//   define default user debug level
#define DEFAULT_USER_TRACE_LEVEL 0

//   define maximum number of parenthesized sub regexes we'll accept
#define MAX_SUBREGEX 256

//   define maximum bracket depth nesting we'll allow....
#define MAX_BRACKETDEPTH 256

//   define maximum number of iterations allowed for EVAL expansion
//#define MAX_EVAL_ITERATIONS 16384
//#define MAX_EVAL_ITERATIONS 1024
#define MAX_EVAL_ITERATIONS 4096

//   define maximum size of a pattern in bytes
#define MAX_PATTERN 16384

//    and how long can a variable name be
#define MAX_VARNAME 2048

//   define the default number of bytes in a learning file hash table
//   (note that this should be a prime number, or at least one with a
//    lot of big factors)
//
//       this value (2097153) is one more than 2 megs, for a .css of 24 megs
//#define DEFAULT_SPARSE_SPECTRUM_FILE_LENGTH 2097153
//
//       this value (1048577) is one more than a meg, for a .css of 12 megs
//       for the Markovian, and half that for OSB classifiers
//
// http://primes.utm.edu/lists/small/100000.txt
//
#define DEFAULT_SPARSE_SPECTRUM_FILE_LENGTH 3396997           /* 6435616333396997 (* 1048573 (* 1048577 */
#define DEFAULT_MARKOVIAN_SPARSE_SPECTRUM_FILE_LENGTH 3396997 /* 6435616333396997 (* 1048573 (* 1048577 */
#define DEFAULT_OSB_BAYES_SPARSE_SPECTRUM_FILE_LENGTH 3396997 /* 6435616333396997 (* 1048573 (* 524287 // Mersenne prime */
#define DEFAULT_WINNOW_SPARSE_SPECTRUM_FILE_LENGTH 3396997    /* 6435616333396997 (* 1048573 (* 1048577 */
//#define DEFAULT_BIT_ENTROPY_FILE_LENGTH 2000000
#define DEFAULT_BIT_ENTROPY_FILE_LENGTH 1000000

//    For the hyperspace matcher, we need to define a few things.
#define HYPERSPACE_MAX_FEATURE_COUNT 500000

//   Stuff for bit-entropic configuration
//   Define the size of our alphabet, and how many bits per alph.
#define ENTROPY_ALPHABET_SIZE 2
#define ENTROPY_CHAR_SIZE 1
#define ENTROPY_CHAR_BITMASK 0x1
//       What fraction of the nodes in a bit-entropic file should be
//       referenceable from the FIR prior arithmetical encoding
//       lookaside table?  0.01 is 1% == average of 100 steps to find
//       the best node.  0.2 is 20% or 5 steps to find the best node.
#define BIT_ENTROPIC_FIR_LOOKASIDE_FRACTION 0.1
#define BIT_ENTROPIC_FIR_LOOKASIDE_STEP_LIMIT 128
#define BIT_ENTROPIC_FIR_PRIOR_BIT_WEIGHT 0.5
#define BIT_ENTROPIC_SHUFFLE_HEIGHT 1024  //   was 256
#define BIT_ENTROPIC_SHUFFLE_WIDTH 1024   //   was 256
#define BIT_ENTROPIC_PROBABILITY_NERF 0.0000000000000000001

//    define the maximum length of a filename
// #define MAX_FILE_NAME_LEN 255

// defaults to system's, if any
#ifdef NAME_MAX
#define MAX_FILE_NAME_LEN (NAME_MAX + 1)
#else
#ifdef FILENAME_MAX
#define MAX_FILE_NAME_LEN (FILENAME_MAX + 1)
#else
#define MAX_FILE_NAME_LEN 262
#endif
#endif

//    define how many microseconds to sleep waiting for a minion process
//    to complete:
//#define MINION_SLEEP_USEC 1000000
//#define MINION_SLEEP_USEC 10000
//#define MINION_SLEEP_USEC 1000
//#define MINION_SLEEP_USEC 100
#define MINION_SLEEP_USEC 10

//    How many microseconds to sleep if we're looping on input WINDOW stmt.
//    try 1 millisecond for now
#define INPUT_WINDOW_SLEEP_USEC 1000

//     Maximum number of different .CSS files in a CLASSIFY
#define MAX_CLASSIFIERS 128

//     Maximum number of nonfatal errors we'll allow before tossing our
//     cookies on a fatal error
#define MAX_NONFATAL_ERRORS 100

//     How big is a feature bucket?  Is it a byte, a short, a long,
//     a float, whatever.  :)
#define FEATUREBUCKET_TYPE FEATUREBUCKET_STRUCT
//#define FEATUREBUCKET_VALUE_MAX 32767
#define FEATUREBUCKET_VALUE_MAX 1000000000
#define FEATUREBUCKET_HISTOGRAM_MAX 4096
//#define FEATUREBUCKET_TYPE unsigned short


////////////////////////////////////////////
//
//      Improved FSCM-specific parameters
//
/////////////////////////////////////////////

//   this is 2^18 + 1
//   This determines the tradeoff in memory vs. speed/accuracy.
//define FSCM_DEFAULT_HASH_TABLE_SIZE 262145
//
//   This is 1 meg + 1
#define FSCM_DEFAULT_HASH_TABLE_SIZE 1048577

//   How long are our prefixes?  Original prefix was 3 but that's
//   rather suboptimal for best speed.  6 looks pretty good for speed and
//   accuracy.
//   prefix length 6 and thickness 10 (200 multiplier) yields 29 / 4147
//
//#define FSCM_DEFAULT_CODE_PREFIX_LEN 3
#define FSCM_DEFAULT_CODE_PREFIX_LEN 6

//  The chain cache is a speedup for the FSCM match
//  It's indexed modulo the chainstart, with associativity 1.0
#define FSCM_CHAIN_CACHE_SIZE 1048577

////////////////////////////////////////////
//
//     Neural Net parameters
//
////////////////////////////////////////////
#define NN_RETINA_SIZE 8192
#define NN_FIRST_LAYER_SIZE 8
#define NN_HIDDEN_LAYER_SIZE 16
#define NN_MAX_FEATURES 65536

//     Neural Net training setups
//
//     Note- convergence seems to work well at
//    alpha 0.2 init_noise 0.5 stoch_noise 0.1 gain_noise 0.00000001
//    alpha 0.2 init_noise 0.2 stoch_noise 0.1 gain_noise 0.00000001
//    alpha 0.2 init_noise 0.2 stoch_noise 0.05 gain_noise 0.00000001
//    alpha 0.2 init_noise 0.2 stoch_noise 0.05 gain_noise 0.00000001
//    alpha 0.2 init_noise 0.2 stoch_noise 0.05 gain_noise 2.0
//    alpha 0.2 init_noise 0.2 stoch_noise 0.05 gain_noise 2.0 zerotr 0.9999

#define NN_DEFAULT_ALPHA 0.2
//   Initialization noise magnitude
#define NN_INITIALIZATION_NOISE_MAGNITUDE 0.2
//   Stochastic noise magnitude
#define NN_DEFAULT_STOCH_NOISE 0.05
//   Gain noise magnitude
#define NN_DEFAULT_GAIN_NOISE 2.0
//   Zero-tracking factor - factor the weights move toward zero every epoch
#define NN_ZERO_TRACKING 0.9999
//   Threshold for back propagation
#define NN_INTERNAL_TRAINING_THRESHOLD 0.1
//  Just use 1 neuron excitation per token coming in.
#define NN_N_PUMPS 1
//  How many training cycles before we punt out
#define NN_MAX_TRAINING_CYCLES 500
//  When doing a "nuke and retry", allow this many training cycles.
#define NN_MAX_TRAINING_CYCLES_FROMSTART 5000
//  How many times to allow a punt?
#define NN_FROMSTART_PUNTING 1000000
//  After how many "not needed" cycles do we microgroom this doc away?
#define NN_MICROGROOM_THRESHOLD 1000000
//  use the sparse retina design?  No, it's not good.
#define NN_SPARSE_RETINA 0

//    End of configurable parameters.




// #define CRM_WITH_OLD_HASH_FUNCTION         1 -- [i_a] not used anymore

#define CRM114_TEXT_HEADERBLOCK_SIZE       (4 * 1024)
#define CRM114_MACHINE_HEADERBLOCK_SIZE    (4 * 1024)
#define CRM114_HEADERBLOCK_SIZE            (CRM114_TEXT_HEADERBLOCK_SIZE + CRM114_MACHINE_HEADERBLOCK_SIZE)


// which classifiers are 'experimental' for this release?
#if defined(CRM_PRODUCTION_CLASSIFIERS_ONLY)
#if CRM_PRODUCTION_CLASSIFIERS_ONLY

// HOWEVER, if the user defined any of these to be EXPLICITLY included, alas, let 'em have it!

#ifndef CRM_WITHOUT_NEURAL_NET
#define CRM_WITHOUT_NEURAL_NET 1
#endif

#ifndef CRM_WITHOUT_SKS
#define CRM_WITHOUT_SKS 1
#endif

#ifndef CRM_WITHOUT_SVM
#define CRM_WITHOUT_SVM 1
#endif

#ifndef CRM_WITHOUT_CLUMP
#define CRM_WITHOUT_CLUMP 1
#endif

#endif /* CRM_PRODUCTION_CLASSIFIERS_ONLY != 0 */
#endif /* defined(CRM_PRODUCTION_CLASSIFIERS_ONLY) */




// now make sure all the classifiers have their enable/disable flag set for compile time inclusion:

#if !defined(CRM_WITHOUT_BIT_ENTROPY)
#define CRM_WITHOUT_BIT_ENTROPY       0
#endif

#if !defined(CRM_WITHOUT_CLUMP)
#define CRM_WITHOUT_CLUMP       0
#endif

#if !defined(CRM_WITHOUT_CORRELATE)
#define CRM_WITHOUT_CORRELATE       0
#endif

#if !defined(CRM_WITHOUT_FSCM)
#define CRM_WITHOUT_FSCM       0
#endif

#if !defined(CRM_WITHOUT_MARKOV)
#define CRM_WITHOUT_MARKOV       0
#endif

#if !defined(CRM_WITHOUT_NEURAL_NET)
#define CRM_WITHOUT_NEURAL_NET       0
#endif

#if !defined(CRM_WITHOUT_OSBF)
#define CRM_WITHOUT_OSBF       0
#endif

#if !defined(CRM_WITHOUT_OSB_BAYES)
#define CRM_WITHOUT_OSB_BAYES       0
#endif

#if !defined(CRM_WITHOUT_OSB_HYPERSPACE)
#define CRM_WITHOUT_OSB_HYPERSPACE       0
#endif

#if !defined(CRM_WITHOUT_OSB_WINNOW)
#define CRM_WITHOUT_OSB_WINNOW       0
#endif

#if !defined(CRM_WITHOUT_SKS)
#define CRM_WITHOUT_SKS       0
#endif

#if !defined(CRM_WITHOUT_SVM)
#define CRM_WITHOUT_SVM       0
#endif






// a special variable which holds the last error report for watching in the debugger.
//
// Of course, the varname could be reached from the CRM script
// itself too, but that's not the intent.
#define HIDDEN_DEBUG_FAULT_REASON_VARNAME       ":_?:"

// with that also comes a special NEGATIVE VALUED 'signal' value for the debugger enable counter:
//
// -2 signals the debugger is currently DISabled but will become ENabled in the future
//    due to a 'debug' statement in the script.
//
// -3 signals that the debugger will NEVER be enabled during this run.
//
// -1 means the debugger is temporarily disabled by a debugger instruction to be re-ENabled
//    when hitting a breakpoint.
//
#define DEBUGGER_DISABLED_FOREVER                       -3


//
// Set this to non-zero to turn on the new 'full compiler' support: arguments are parsed at an early stage,
// reduced JITting and IMPROVED error checking!
//
#define FULL_PARSE_AT_COMPILE_TIME                      1



#endif /* __CRM114_CONFIG_H__ */

