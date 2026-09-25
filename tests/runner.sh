#!/usr/bin/env bash
# shellcheck disable=SC2181
# shellcheck disable=SC2231

#    a simple color library written in bash - themis
#    Copyright (C) 2021 lazypwny751
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.

# Reset
reset='\033[0m'           # Text Reset

# Regular Colors
black='\033[0;30m'        # Black
red='\033[0;31m'          # Red
green='\033[0;32m'        # Green
yellow='\033[0;33m'       # Yellow
blue='\033[0;34m'         # Blue
purple='\033[0;35m'       # Purple
cyan='\033[0;36m'         # Cyan
white='\033[0;37m'        # White

# Bold
Bblack='\033[1;30m'       # Black
Bred='\033[1;31m'         # Red
Bgreen='\033[1;32m'       # Green
Byellow='\033[1;33m'      # Yellow
Bblue='\033[1;34m'        # Blue
Bpurple='\033[1;35m'      # Purple
Bcyan='\033[1;36m'        # Cyan
Bwhite='\033[1;37m'       # White

# Underline
Ublack='\033[4;30m'       # Black
Ured='\033[4;31m'         # Red
Ugreen='\033[4;32m'       # Green
Uyellow='\033[4;33m'      # Yellow
Ublue='\033[4;34m'        # Blue
Upurple='\033[4;35m'      # Purple
Ucyan='\033[4;36m'        # Cyan
Uwhite='\033[4;37m'       # White

# Background
BGblack='\033[40m'        # Black
BGred='\033[41m'          # Red
BGgreen='\033[42m'        # Green
BGyellow='\033[43m'       # Yellow
BGblue='\033[44m'         # Blue
BGpurple='\033[45m'       # Purple
BGcyan='\033[46m'         # Cyan
BGwhite='\033[47m'        # White

# High Intensity
Iblack='\033[0;90m'       # Black
Ired='\033[0;91m'         # Red
Igreen='\033[0;92m'       # Green
Iyellow='\033[0;93m'      # Yellow
Iblue='\033[0;94m'        # Blue
Ipurple='\033[0;95m'      # Purple
Icyan='\033[0;96m'        # Cyan
Iwhite='\033[0;97m'       # White

# Bold High Intensity
BIblack='\033[1;90m'      # Black
BIred='\033[1;91m'        # Red
BIgreen='\033[1;92m'      # Green
BIyellow='\033[1;93m'     # Yellow
BIblue='\033[1;94m'       # Blue
BIpurple='\033[1;95m'     # Purple
BIcyan='\033[1;96m'       # Cyan
BIwhite='\033[1;97m'      # White

# High Intensity backgrounds
BGIblack='\033[0;100m'   # Black
BGIred='\033[0;101m'     # Red
BGIgreen='\033[0;102m'   # Green
BGIyellow='\033[0;103m'  # Yellow
BGIblue='\033[0;104m'    # Blue
BGIpurple='\033[0;105m'  # Purple
BGIcyan='\033[0;106m'    # Cyan
BGIwhite='\033[0;107m'   # White

# lolbash is a function that offers random colors similar to lolcat
# Usage: echo -e "$(randomcolor) hello${reset}"

randomcolor() {
    case ${1} in
        simple)
            echo -ne "\e[3$(( $RANDOM * 6 / 32767 + 1 ))m"
        ;;
        cool)
            local bold=$(( $RANDOM % 2 ))
            local code=$(( 30 + $RANDOM % 8 ))
            printf "%d;%d\n" $bold $code
        ;;
    esac
}

# lolbash is a function that offers random colors similar to lolcat for every character(s)
# Usage: echo -e "$(lolbash) hello${reset}"

lolbash() {
	sentence="$*"
	for (( i=0; i<${#sentence}; i++ )); do
	    printf "\e[%sm%c" "$(random_colour)" "${sentence:i:1}"
	done
	echo -e '\e[0m'
}

[ $(echo $BASH_VERSION | egrep -Eo '[[:digit:]]' | head -n1) -lt 4 ] && {
  echo -e "${Bred}error${reset}: unsupported bash version, please install at least version 4"
  if [[ "${OSTYPE}" =~ "darwin" ]]
  then
    echo -e "${Bblue}note:${reset} install bash by running 'brew install bash'"
  fi
  exit 1
}

timestamp() {
  if [[ "${OSTYPE}" =~ "darwin" ]]
  then
    if hash gdate 2>/dev/null; then
        gdate +%s%N
    else
        date +%s
    fi
  else
    date +%s%N
  fi
}

TESTS_DIR=$(realpath $(dirname "$0"))
CXY_DEFAULT_COMMAND=./cxy
CXY_STDLIB=./stdlib
CXY_DEFAULT_ARGS="dev --dump-ast CXY --no-color --no-progress --clean-ast --warnings=~RedundantStmt"
CXY_DEFAULT_SNAPSHOT_EXT=".cxy"

POSITIONAL_ARGS=()
while [[ $# -gt 0 ]]; do
  case $1 in
    -u|--update-snapshot)
      UPDATE_SNAPSHOT=1
      shift # past argument
      ;;
    -s|--suite)
      SUITE_FILTER="$2"
      shift # past argument
      shift # past value
      ;;
    -f|--test-filter)
      TEST_FILTER="$2"
      shift # past argument
      shift # past value
      ;;
    -d|--test-dir)
      TESTS_DIR=$(realpath "$2")
      shift # past argument
      shift # past value
      ;;
    --cxy)
      CXY_COMPILER="$2"
      CXY_DEFAULT_COMMAND="$2"
      shift # past argument
      shift # past value
      ;;
    --lib)
      CXY_STDLIB="$2"
      shift # past argument
      shift # past value
      ;;
    --no-check)
      CXY_FILE_CHECK_OFF=1
      shift # past argument
      ;;
    --default)
      DEFAULT=YES
      shift # past argument
      ;;
    -*|--*)
      echo "Unknown option $1"
      exit 1
      ;;
    *)
      POSITIONAL_ARGS+=("$1") # save positional arg
      shift # past argument
      ;;
  esac
done

# Find run configuration up directory stack (returns run.cfg or suites/*.cfg)
__find_configs() {
  local dir=$1
  while [ -n "$dir" ] && [ "$dir" != "/" ]
  do
    if [ -d "$dir/suites" ]; then
      local found_suites=( "$dir/suites"/*.cfg )
      if [ -e "${found_suites[0]}" ]; then
        echo "${found_suites[@]}"
        return 0
      fi
    fi
    if [ -e "$dir/run.cfg" ]; then
      echo "$dir/run.cfg"
      return 0
    fi
    if [ "$dir" == "$TESTS_DIR" ]; then
      break
    fi
    dir=$(dirname "$dir")
  done
}

__read_cfg() {
    local cfg_path="$1"
    local req_key="$2"
    local default="$3"

    local comment_prefix=";"
    while read line
    do
      if [ -z "$line" ] || [[ "$line" == $comment_prefix* ]]; then
        continue
      fi
      IFS='=' read -r key value <<< "$line"
      if [ "${key}" == "${req_key}" ]; then
          value="${value%\"}"
          value="${value#\"}"
          echo "$value"
          return 0
      fi
    done < "$cfg_path"

    default="${default%\"}"
    default="${default#\"}"
    echo "$default"
}

__exec_with_leaks() {
  local bin="$1"
  local exec_args="$2"
  local out
  out=$(leaks --atExit --quiet -- "${bin}" ${exec_args} 2>&1)
  local rc=$?

  echo "${out}"
  if [ $rc -ne 0 ] || echo "${out}" | grep -Eq "[1-9][0-9]* leaks for [1-9][0-9]* total leaked bytes"; then
    return 1
  fi
  return 0
}

__exec_with_asan() {
  local bin="$1"
  local exec_args="$2"
  local out
  out=$("${bin}" ${exec_args} 2>&1)
  local rc=$?

  echo "${out}"
  if [ $rc -ne 0 ] || echo "${out}" | grep -Eq "ERROR: (AddressSanitizer|LeakSanitizer)"; then
    return 1
  fi
  return 0
}

run_test () {
  local test_path=$1
  local test_dir=$(dirname "$test_path")
  local test_case="${2}"
  local run_cfg="${3}"

  local snapshot_ext="${CXY_DEFAULT_SNAPSHOT_EXT}"
  local run_args="${CXY_DEFAULT_ARGS}"
  local run_cmd="${CXY_DEFAULT_COMMAND}"
  local execute="false"
  local exec_args=""
  local suite_name=""
  local snapshots_dir="${test_dir}/__snapshots"

  if [ -n "${run_cfg}" ] && [ -f "${run_cfg}" ]; then
    snapshot_ext=$(__read_cfg "$run_cfg" "snapshot_ext" "$snapshot_ext")
    run_args=$(__read_cfg "$run_cfg" "run_args" "$run_args")
    run_cmd=$(__read_cfg "$run_cfg" "command" "$run_cmd")
    execute=$(__read_cfg "$run_cfg" "execute" "false")
    exec_args=$(__read_cfg "$run_cfg" "exec_args" "$exec_args")

    if [[ "$run_cfg" == *"/suites/"* ]]; then
      suite_name=$(basename "$run_cfg" .cfg)
      snapshots_dir="${test_dir}/__snapshots/${suite_name}"
    fi
  fi

  local display_name="${test_case}"
  if [ -n "${suite_name}" ]; then
    display_name="${test_case} [${suite_name}]"
  fi

  echo -e "Running test case ${display_name}"
  local expected="${snapshots_dir}/$(basename "$2")${snapshot_ext}"
  local output=""
  local status=0

  if [ "${execute}" == "true" ]; then
    local bin_tmp=$(mktemp /tmp/cxy_exec_XXXXXX)
    local compile_args="${run_args}"
    local is_darwin=0
    [[ "${OSTYPE}" =~ "darwin" ]] && is_darwin=1

    if [ $is_darwin -eq 1 ] && hash leaks 2>/dev/null; then
      compile_args=$(echo "${run_args}" | sed -E 's/--cflags(=[[:blank:]]*)?-fsanitize=address//g; s/-fsanitize=address//g')
    fi

    output=$(${run_cmd} ${compile_args} -o "${bin_tmp}" "${test_path}" 2>&1)
    status=$?
    if [ $status -eq 0 ] && [ -f "${bin_tmp}" ]; then
      if [ $is_darwin -eq 1 ] && hash leaks 2>/dev/null; then
        output=$(__exec_with_leaks "${bin_tmp}" "${exec_args}")
        status=$?
      else
        output=$(__exec_with_asan "${bin_tmp}" "${exec_args}")
        status=$?
      fi
    fi
    rm -f "${bin_tmp}"
  else
    output=$(${run_cmd} ${run_args} "${test_path}" 2>&1)
    status=$?
  fi

  if ! grep -m 1 -e "^//[[:blank:]]*@TEST:[[:blank:]]*FileCheck"  "$test_path" > /dev/null ; then
    # Regular snapshot test
    if [ $status -ne 0 ]; then
      expected="${snapshots_dir}/$(basename "$2").fail"
      if [ ! -e "$expected" ] ; then
        echo -e "  ${Bred}FAILED${reset}: Execution failed for test case ${display_name}"
        echo -e "${output}"
        return 1
      fi
    elif [ -e "${snapshots_dir}/$(basename "$2").log" ] ; then
      expected="${snapshots_dir}/$(basename "$2").log"
    fi

    if [ -n "${UPDATE_SNAPSHOT}" ]
    then
      mkdir -p "${snapshots_dir}"
      echo "${output}" > "${expected}"
    fi

    if [ -f "$expected" ] ; then
      if diff --color=always /dev/null /dev/null 2>/dev/null; then
        match=$(diff --color=always -U2 --label "Expected: ${expected}" --label "Actual: ${test_path}" "${expected}" <(echo "${output}"))
      else
        match=$(git --no-pager diff --no-index --color=always -U2 "${expected}" <(echo "${output}"))
      fi
      if [ $? -ne 0 ]; then
          echo -e "  ${Bred}FAILED${reset}: Output does not match expected snapshot"
          echo "${match}"
          return 1
      fi
    fi
  else
    local file_check=FileCheck
    if [ -z "${CXY_FILE_CHECK_OFF}" ]; then
      [ -n "${FILE_CHECK}" ] && file_check="${FILE_CHECK}"

      match=$(echo "${output}" | "${file_check}" "$test_path")
      [ $? -ne 0 ] && {
          echo -e "  ${Bred}FAILED${reset}: FileCheck failed to verify compiler output"
          echo -e "${match}"
          return 1
      }
    else
      echo "${output}"
    fi
  fi

  return 0
}

TESTS_START=$(timestamp)
FAILED_TESTS=0
PASSED_TESTS=0
declare -A TEST_RESULTS
declare -A TEST_DURATIONS
for test in $(find $TESTS_DIR -type f -name '*.cxy' -not -path '**/__snapshots/*') ; do
  if [ -n "${TEST_FILTER}" ] && ! echo $test | grep -Eq "${TEST_FILTER}"
  then
    continue
  fi

  test_case="${test#"$TESTS_DIR"/}"
  test_case=${test_case%.*}
  test_dir=$(dirname "$test")

  configs=( $(__find_configs "$test_dir") )
  if [ ${#configs[@]} -eq 0 ]; then
    configs=("")
  fi

  for cfg in "${configs[@]}"; do
    suite_name=""
    if [[ "$cfg" == *"/suites/"* ]]; then
      suite_name=$(basename "$cfg" .cfg)
      run_key="${test_case} [${suite_name}]"
    else
      run_key="${test_case}"
    fi

    if [ -n "${SUITE_FILTER}" ] && [ -n "${suite_name}" ] && ! echo "${suite_name}" | grep -Eq "${SUITE_FILTER}"; then
      continue
    fi

    test_start=$(timestamp)
    run_test "${test}" "${test_case}" "${cfg}"
    status=$?
    test_end=$(timestamp)
    duration=$(((test_end - test_start)/1000000))

    if [ $status -ne 0 ]
    then
      FAILED_TESTS=$((FAILED_TESTS + 1))
      TEST_RESULTS+=(["${run_key}"]="FAILED")
    else
      PASSED_TESTS=$((PASSED_TESTS + 1))
      TEST_RESULTS+=(["${run_key}"]="PASSED")
    fi
    TEST_DURATIONS+=(["${run_key}"]=${duration})
  done
done
TESTS_DONE=$(timestamp)

echo -e "----------------------------------------------------------"
echo -en "Results: "
if [ ${FAILED_TESTS} -eq 0 ]; then
  echo -e "${Bgreen}PASSED${reset}"
else
  echo -e "${Bred}FAILED${reset}"
fi

for test in "${!TEST_RESULTS[@]}"; do
  status=${TEST_RESULTS[$test]}
  duration=${TEST_DURATIONS[$test]}
  if [ "${status}" = "PASSED" ]; then
    echo -ne "${Bgreen}"
    printf "  \xE2\x9C\x94 "
    echo -e "${reset}  ${test} ... ${Bgreen}${status}${reset} (${duration} ms)"
  else
    echo -ne "${Bred}"
    printf "  \xE2\x9C\x98 "
    echo -e "${reset}  ${test} ... ${Bred}${status}${reset} (${duration} ms)"
  fi
done

duration=$(((TESTS_DONE - TESTS_START)/1000000))
echo
echo -e "Summary"
echo -en "  Passed: ${Bgreen}${PASSED_TESTS}${reset} Failed: "
[ "${FAILED_TESTS}" -ne 0 ] && echo -en "${Bred}"
echo -e "${FAILED_TESTS}${reset}"
echo -e "  Elapsed ${duration} ms"
echo -e "----------------------------------------------------------"
[ "${FAILED_TESTS}" -ne 0 ] && exit 1
exit 0
