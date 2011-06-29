#!/bin/sh

#
# testscript <scriptfile> <inputfile> <expected-output-file> <diff-process-script> <extra-script-args>
#
# runs CRM114 script <scriptfile> 
# feeding it <inputfile> on stdin (if the parameter is non-empty)
# and <extra-script-args> on the commandline
#
# output is redirected to a temporary file, which is compared against
# <expected-output-file> using the <diff-process-script>.
#
# Of course, the latter usually is nothing more than a little shell script
# around 'diff -u', but when you test non-text or 'imprecise' test cases 
# (checking the pR result for a classify is a good example of the latter)
# you may want to perform other actions to compare the results with what's
# expected.
#
# return exit code 0 on success; any other return code for test failure
#

CRM_BIN="/windows/G/prj/3actual/crm114/src/crm114"


report_error()
{
  echo ""
  echo "Format:"
  echo "  ./testscript.sh <scriptfile> <inputfile> <expected-output-file> <diff-process-script> <extra-script-args>"
  echo ""
  echo "**ERROR**:"
  echo "  $1"
}

report_warning()
{
  echo "**WARNING**:"
  echo "  $1"
  echo ""
}

report_msg()
{
  echo "  $1"
}


scriptfile="$1"
expected="$3"
diffscript="$4"
inputdata="$2"

tmpout="$0.tempout"
tmperr="$0.temperr"
diffout="$0.diffout"

if [ ! -f "${scriptfile}" ]; then
  echo "script file '"${scriptfile}"' does not exist!"
  exit 66
fi

# diffscript may be NIL, must otherwise exist as file
if [ ! -z "${diffscript}" ]; then
  if [ ! -f "${diffscript}" ]; then
    report_error "diff script '"${diffscript}"' does not exist"
    exit 65
  fi
fi

# expected output may be absent; if it is absent, a fresh file
# will be created.containing a copy of the test output.
# this is handy to generate the 'expected output' files to start off with...
if [ ! -z "${expected}" ]; then
  if [ "x${expected}" = "x-" ]; then
    expected="ref/${scriptfile}.refoutput"
    report_warning "going to use expected outfile file '"${expected}"'..."
  fi
  if [ ! -f "${expected}" ]; then
    report_warning "expected outfile file '"${expected}"' does not exist: script will create this file!"
  fi
else
  report_error "expected outfile file '"${expected}"' does not exist"
  exit 64
fi

shift
shift
shift
shift

rm -f "${tmperr}" 
rm -f "${tmpout}"

# see if inputfile is a nonempty string

report_msg "going to run CRM114 '"${scriptfile}"' with commandline: $@"

if [ -z "${inputdata}" ]; then
  report_warning "input is nil/empty"
  ${CRM_BIN} "${scriptfile}" $@ 2> "${tmperr}" > "${tmpout}"
else
  # see if file exists; if not, assume input to be literal text
  if [ -f "${inputdata}" ]; then
    # run test:
    ${CRM_BIN} "${scriptfile}" $@ < "${inputdata}" 2> "${tmperr}" > "${tmpout}"
  else
    echo "${inputdata}" | ${CRM_BIN} "${scriptfile}" $@ 2> "${tmperr}" > "${tmpout}"
  fi
fi

exit 0



