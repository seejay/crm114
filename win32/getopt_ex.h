/*!
 \file
 */
/* Declarations for getopt.
 * Copyright (C) 1989,90,91,92,93,94,96,97,98 Free Software Foundation, Inc.
 * This file is part of the GNU C Library.
 *
 * The GNU C Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The GNU C Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with the GNU C Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.  */

#ifndef __GETOPT_EX_C_H__
#define __GETOPT_EX_C_H__

#ifdef __cplusplus
extern "C"
{
#endif



/*!
 * For communication from `getopt' to the caller.
 * When `getopt' finds an option that takes an argument,
 * the argument value is returned here.
 * Also, when `ordering' is RETURN_IN_ORDER,
 * each non-option ARGV-element is returned here.
 */
extern char *optarg;

/*!
 * Index in ARGV of the next element to be scanned.
 * This is used for communication to and from the caller
 * and for communication between successive calls to `getopt'.
 *
 * On entry to `getopt', zero means this is the first call; initialize.
 *
 * When `getopt' returns -1, this is the index of the first of the
 * non-option elements that the caller should itself scan.
 *
 * Otherwise, `optind' communicates from one call to the next
 * how much of ARGV has been scanned so far.
 */
extern int optind;

/*!
 * Callers store zero here to inhibit the error message `getopt' prints
 * for unrecognized options.
 */
extern int opterr;

/*!
 * Set to an option character which was unrecognized.
 */
extern int optopt;

/*!
 * Describe the long-named options requested by the application.
 * The LONG_OPTIONS argument to getopt_long or getopt_long_only is a vector
 * of `struct option' terminated by an element containing a name which is
 * zero.
 *
 * The field `has_arg' is:
 *
 * \arg no_argument          (or 0) if the option does not take an argument,
 * \arg required_argument    (or 1) if the option requires an argument,
 * \arg optional_argument    (or 2) if the option takes an optional argument.
 *
 * If the field `flag' is not NULL, it points to a variable that is set
 * to the value given in the field `val' when the option is found, but
 * left unchanged if the option is not found.
 *
 * To have a long-named option do something other than set an `int' to
 * a compiled-in constant, such as set a value from `optarg', set the
 * option's `flag' field to zero and its `val' field to a nonzero
 * value (the equivalent single-letter option character, if there is
 * one).  For long options that have a zero `flag' field, `getopt'
 * returns the contents of the `val' field.
 */
struct option
{
    const char *name;
    /*!
     * has_arg can't be an enum because some compilers complain about
     * type mismatches in all the code that assumes it is an int.
     */
    int has_arg;
    int *flag;
    int val;
};

/*!
 * \defgroup GETOPT_HAS_ARG_FLAGS 'has_arg' values
 *
 *      Names for the values of the `has_arg' field of `struct option'.
 *
 * \{
 */
#define no_argument        0
#define required_argument  1
#define optional_argument  2
/*! \} */

/*!
 * Get definitions and prototypes for functions to process the
 * arguments in ARGV (ARGC of them, minus the program name) for
 * options given in OPTS.
 *
 * Return the option character from OPTS just read.  Return -1 when
 * there are no more options.  For unrecognized options, or options
 * missing arguments, `optopt' is set to the option letter, and '?' is
 * returned.
 *
 * The OPTS string is a list of characters which are recognized option
 * letters, optionally followed by colons, specifying that that letter
 * takes an argument, to be placed in `optarg'.
 *
 * If a letter in OPTS is followed by two colons, its argument is
 * optional.  This behavior is specific to the GNU `getopt'.
 *
 * The argument `--' causes premature termination of argument
 * scanning, explicitly telling `getopt' that there are no more
 * options.
 *
 * If OPTS begins with `--', then non-option arguments are treated as
 * arguments to the option 0.  This behavior is specific to the GNU
 * `getopt'.
 */
typedef void getopt_print_error_f(void *, const char *, ...);
int getopt_ex(int argc, char *const *argv, const char *optstring, getopt_print_error_f *print_error, void *propagate);
int getopt_long_ex(int argc, char *const *argv, const char *options, const struct option *long_options, int *opt_index
               ,getopt_print_error_f *print_error, void *propagate);
int getopt_long_only_ex(int argc, char *const *argv, const char *options, const struct option *long_options, int *opt_index
                    ,getopt_print_error_f *print_error, void *propagate);


#ifdef __cplusplus
}
#endif

#endif /* getopt.h */
