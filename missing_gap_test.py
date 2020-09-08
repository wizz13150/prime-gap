#!/usr/bin/env python3
#
# Copyright 2020 Seth Troisi
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import itertools
import math
import os.path
import re
import subprocess
import time

import gmpy2


def get_arg_parser():
    parser = argparse.ArgumentParser('Test prime gaps generated by gap_search')

    parser.add_argument('--unknown-filename', type=str,
        help="determine mstart, minc, p, d, sieve-length, and sieve-range"
             " from unknown-results filename")

    return parser


def verify_args(args):
    if args.unknown_filename:
        fn = args.unknown_filename
        if not os.path.exists(fn):
            print ("\"{}\" doesn't exist".format(fn))
            exit(1)
        match = re.match(
            "^(\d+)_(\d+)_(\d+)_(\d+)_s(\d+)_l(\d+)M.(?:m2.)?missing.txt",
            os.path.basename(fn))
        if not match:
            print ("\"{}\" doesn't match unknown file format".format(fn))
            exit(1)

        ms, p, d, mi, sl, sr = map(int, match.groups())
        args.mstart = ms
        args.minc = mi
        args.p = p
        args.d = d
        args.sieve_length = sl
        args.sieve_range = sr * 10 ** 6

    for arg in ('mstart', 'minc', 'p', 'd',):
        if arg not in args or args.__dict__[arg] in (None, 0):
            print ("Missing required argument", arg)
            exit(1)


    fn = "{}_{}_{}_{}_s{}_l{}M.m2.missing.txt".format(
        args.mstart, args.p, args.d, args.minc,
        args.sieve_length, args.sieve_range // 10 ** 6)

    if args.unknown_filename:
        assert fn == os.path.basename(args.unknown_filename), (fn, args.unknown_filename)
    else:
        args.unknown_filename = fn


def openPFGW_is_prime(strn):
    # Overhead of subprocess calls seems to be ~0.03
    s = subprocess.getstatusoutput(f"./pfgw64 -e1 -q'{strn}'")
    assert s[1].startswith('PFGW'), s
    return s[0] == 0


def is_prime(num, strnum):
    # TODO print log of which library is being used.
    if gmpy2.num_digits(num, 2) > 5000:
        return openPFGW_is_prime(strnum)

    return gmpy2.is_prime(num)


def prime_gap_test(args):
    P = args.p
    D = args.d

    # used in next_prime
    assert P <= 80000
    K = gmpy2.primorial(P)
    K, r = divmod(K, D)
    assert r == 0

    K_digits = gmpy2.num_digits(K, 10)
    K_bits   = gmpy2.num_digits(K, 2)
    K_log    = float(gmpy2.log(K))
    print("K = {} bits, {} digits, log(K) = {:.2f}".format(
        K_bits, K_digits, K_log))

    # ----- Open Output file
    print("\tLoading unknowns from '{}'".format(args.unknown_filename))
    print()


    # ----- Main sieve loop.
    print()

    # Used for various stats
    s_start_t = time.time()
    s_last_print_t = time.time()

    tested_m = 0
    tested = 0
    primes = 0
    with open(args.unknown_filename) as unknown_file:
        for li, line in enumerate(unknown_file):
            tested_m += 1

            # split of the first part.
            start, to_test = line.split(" : ")
            start = start.replace(' ', '')

            print ("start: {}\t tested_m: {}, tested: {} => primes: {} | tests for this m {}".format(
                start, tested_m, tested, primes, to_test.count('(')))
            m, p, d = re.match(r"(\d+)\*(\d+)#\/(\d+)", start).groups()
            assert p == str(P) and d == str(D), (p, d, P, D)

            Mi = int(m)

            N = Mi * K

            last_low = None
            last_low_status = None
            for match in re.findall("(\d+, \d+)", to_test):
                low, high = match.split(", ")

                if last_low_status and low > last_low:
                    # if last was prime then stop.
                    break

                if low != last_low or not last_low_status:
                    tested += 1
                    last_low_status = is_prime(N - int(low), start + "-" + low)
                    last_low = low

                if last_low_status:
                    primes += 1
                    if is_prime(N + int(high), start + "+" + high):
                        primes += 1
                        print("\tBOTH SIDES PRIME:", start, low, high)


            s_stop_t = time.time()
            print_secs = s_stop_t - s_last_print_t
            if li in (1,10,30,100,300,1000) or li % 5000 == 0 or print_secs > 240:
                secs = s_stop_t - s_start_t
                s_last_print_t = s_stop_t

                def roundSig(n, sig):
                    return '{:g}'.format(float('{:.{p}g}'.format(n, p=sig)))

                # Want 3 sig figs which is hard in python
                if tested and tested < secs:
                    timing = "{} secs/test".format(roundSig(secs / tested, 3))
                else:
                    timing = "{}/sec".format(roundSig(tested / secs, 3))

                # Stats!
                print("\t{} {}    tests     {:<10d} ({})  {:.0f} seconds elapsed".format(
                    li, tested_m, tested, timing, secs))


if __name__ == "__main__":
    parser = get_arg_parser()
    args = parser.parse_args()
    verify_args(args)

    prime_gap_test(args)

