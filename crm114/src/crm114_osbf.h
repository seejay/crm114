//  crm114_osbf.h  - Controllable Regex Mutilator,  version v1.0
//  Copyright 2001-2009  William S. Yerazunis, all rights reserved.
//
//  This software is licensed to the public under the Free Software
//  Foundation's GNU GPL, version 2.  You may obtain a copy of the
//  GPL by visiting the Free Software Foundations web site at
//  www.fsf.org, and a copy is included in this distribution.
//
//  This file defines CSS header structure, data and constants used
//  by the OSBF-Bayes classifier.  -- Fidelis Assis - 2004/10/20
//

#ifndef __CRM114_OBSF_H__
#define __CRM114_OBSF_H__

#ifdef __cplusplus
extern "C"
{
#endif



typedef struct
{
    crmhash_t hash;
    crmhash_t key;
    uint32_t value;
} OSBF_FEATUREBUCKET_STRUCT;

typedef struct
{
    unsigned char version[4];
    uint32_t flags;
    uint32_t buckets_start;      /* offset to first bucket, in bucket size units */
    uint32_t buckets;            /* number of buckets in the file */
    uint64_t learnings;          /* number of trainings executed */
} OSBF_FEATURE_HEADER_STRUCT;

/* define header size to be a multiple of bucket size with aprox. 4 Kbytes */
#define OSBF_CSS_SPECTRA_START (4096 / sizeof(OSBF_FEATUREBUCKET_STRUCT))

/* complete header */
typedef union
{
    OSBF_FEATURE_HEADER_STRUCT header;
    /*   buckets in header - not really buckets, but the header size is */
    /*   a multiple of bucket size */
    OSBF_FEATUREBUCKET_STRUCT bih[OSBF_CSS_SPECTRA_START];
} OSBF_HEADER_UNION;

#define BUCKET_VALUE_MASK 0x0000FFFFLU
#define BUCKET_LOCK_MASK  0x80000000LU
#define BUCKET_HASH(bucket) (bucket.hash)
#define BUCKET_KEY(bucket) (bucket.key)
#define BUCKET_RAW_VALUE(bucket) (bucket.value)
#define VALID_BUCKET(header, bucket_idx) (bucket_idx < header->buckets)
#define GET_BUCKET_VALUE(bucket) ((bucket.value) & BUCKET_VALUE_MASK)
#define BUCKET_IS_LOCKED(bucket) ((bucket.value) & BUCKET_LOCK_MASK)
#define SETL_BUCKET_VALUE(bucket, val) \
    (bucket.value) = (val)             \
                     | BUCKET_LOCK_MASK
#define SET_BUCKET_VALUE(bucket, val) (bucket.value) = val
#define LOCK_BUCKET(bucket) (bucket.value) = (bucket.value) | BUCKET_LOCK_MASK
#define UNLOCK_BUCKET(bucket)         \
    (bucket.value) = (bucket.value)   \
                     & BUCKET_VALUE_MASK
#define BUCKET_IN_CHAIN(bucket) (GET_BUCKET_VALUE(bucket) != 0)
#define EMPTY_BUCKET(bucket) (GET_BUCKET_VALUE(bucket) == 0)
#define BUCKET_HASH_COMPARE(bucket, h, k) \
    ((bucket.hash) == (h)                 \
    && (bucket.key) == (k))

/* CSS file version */
#define SBPH_VERSION            0
#define OSB_VERSION             1
#define CORRELATE_VERSION       2
#define NEURAL_VERSION          3
#define OSB_WINNOW_VERSION      4
#define OSBF_VERSION            5
#define UNKNOWN_VERSION         6

/*
 * Array with pointers to CSS version names, indexed with the
 * CSS file version numbers above. The array is defined in
 * crm_osbf_maintenance.c
 */
extern char *CSS_version_name[];

/* max feature count */
#define OSBF_FEATUREBUCKET_VALUE_MAX 65535

//
// http://primes.utm.edu/lists/small/100000.txt
// http://primes.utm.edu/curios/page.php?rank=3019
//
#define OSBF_DEFAULT_SPARSE_SPECTRUM_FILE_LENGTH   3396997 /* 6435616333396997 (* 1048573 (* 94321 */

/* max chain len - microgrooming is triggered after this, if enabled */
#define OSBF_MICROGROOM_CHAIN_LENGTH 29
/* maximum number of buckets groom-zeroed */
#define OSBF_MICROGROOM_STOP_AFTER 128
/* minimum ratio between max and min P(F|C) */
#define OSBF_MIN_PMAX_PMIN_RATIO 1
/* max token size before starting "accumulation" of tokens */
#define OSBF_MAX_TOKEN_SIZE 60
/* accumulate hashes up to this many tokens */
#define OSBF_MAX_LONG_TOKENS 1000

extern int crm_expr_osbf_bayes_learn(CSL_CELL *csl, ARGPARSE_BLOCK *apb,
        VHT_CELL **vht,
        CSL_CELL *tdw,
        char *txtptr, int txtoffset, int txtlen);
extern int crm_expr_osbf_bayes_classify(CSL_CELL *csl,
        ARGPARSE_BLOCK *apb,
        VHT_CELL **vht,
        CSL_CELL *tdw,
        char *txtptr, int txtoffset, int txtlen);

extern int crm_expr_osbf_bayes_css_merge(CSL_CELL *csl,
        ARGPARSE_BLOCK *apb,
        char *txtptr, int txtoffset, int txtlen);
extern int crm_expr_osbf_bayes_css_diff(CSL_CELL *csl,
        ARGPARSE_BLOCK *apb,
        char *txtptr, int txtoffset, int txtlen);
extern int crm_expr_osbf_bayes_css_backup(CSL_CELL *csl,
        ARGPARSE_BLOCK *apb,
        char *txtptr, int txtoffset, int txtlen);
extern int crm_expr_osbf_bayes_css_restore(CSL_CELL *csl,
        ARGPARSE_BLOCK *apb,
        char *txtptr, int txtoffset, int txtlen);
extern int crm_expr_osbf_bayes_css_info(CSL_CELL *csl,
        ARGPARSE_BLOCK *apb,
        char *txtptr, int txtoffset, int txtlen);
extern int crm_expr_osbf_bayes_css_analyze(CSL_CELL *csl,
        ARGPARSE_BLOCK *apb,
        char *txtptr, int txtoffset, int txtlen);
extern int crm_expr_osbf_bayes_css_create(CSL_CELL *csl,
        ARGPARSE_BLOCK *apb,
        char *txtptr, int txtoffset, int txtlen);
extern int crm_expr_osbf_bayes_css_migrate(CSL_CELL *csl,
        ARGPARSE_BLOCK *apb,
        char *txtptr, int txtoffset, int txtlen);

extern void crm_osbf_set_microgroom(int value);
extern void crm_osbf_microgroom(OSBF_FEATURE_HEADER_STRUCT *h,
        unsigned int hindex);
extern void crm_osbf_packcss(OSBF_FEATURE_HEADER_STRUCT *h,
        unsigned int packstart, unsigned int packlen);
extern void crm_osbf_packseg(OSBF_FEATURE_HEADER_STRUCT *h,
        unsigned int packstart, unsigned int packlen);
extern unsigned int crm_osbf_next_bindex(OSBF_FEATURE_HEADER_STRUCT *header,
        unsigned int index);
extern unsigned int crm_osbf_prev_bindex(OSBF_FEATURE_HEADER_STRUCT *header,
        unsigned int index);
extern unsigned int crm_osbf_find_bucket(OSBF_FEATURE_HEADER_STRUCT *header,
        unsigned int hash, unsigned int key);
extern void crm_osbf_update_bucket(OSBF_FEATURE_HEADER_STRUCT *header,
        unsigned int bindex, int delta);
extern void crm_osbf_insert_bucket(OSBF_FEATURE_HEADER_STRUCT *header,
        unsigned int bindex, unsigned int hash,
        unsigned int key, int value);
extern int crm_osbf_create_cssfile(char *cssfile, unsigned int buckets,
        unsigned int major, unsigned int minor /* [i_a] unused anyway ,
                                                * unsigned int spectrum_start */);



#ifdef __cplusplus
}
#endif

#endif /* __CRM114_OBSF_H__ */

