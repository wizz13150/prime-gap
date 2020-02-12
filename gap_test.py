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

import math
import re
import time

import gmpy2

def prime_gap_test():
    # TODO argparse and config

    '''
    M = 1
    M_inc = 10 ** 6
    P = 503
    D = 1
    min_merit = 18

    SL = sieve_length = 1902
    sieve_range = 200000000
    '''

    M = 1
    M_inc = 2000
    P = 1999
    D = 1
    min_merit = 18

    SL = sieve_length = 7550
    sieve_range = 2000000000

    run_prp = True

    K = gmpy2.primorial(P)
    assert K % D == 0
    K //= D

    K_digits = gmpy2.num_digits(K, 10)
    K_bits   = gmpy2.num_digits(K, 2)
    K_log    = gmpy2.log(K)
    print("K = {} bits, {} digits, log(K) = {:.2f}".format(
        K_bits, K_digits, K_log))
    print("Min Gap ~= {} (for merit > {:.1f})\n".format(
        int(min_merit * (K_log + math.log(M))), min_merit))

    # ----- Open Output file
    fn = "{}_{}_{}_{}_s{}_l{}M.txt".format(
        M, P, D, M_inc, sieve_length, sieve_range // 10 ** 6)
    print("\tSaving unknowns to '{}'".format(fn))
    print()
    unknown_file = open(fn, "r")

    # used in next_prime
    assert P <= 80000
    # Very slow but works.
    primes = [2] + [p for p in range(3, 80000+1, 2) if gmpy2.is_prime(p)]
    K_primes = [p for p in primes if p <= P]

    # ----- Allocate memory for a handful of utility functions.

    # Remainders of (p#/d) mod prime
    remainder   = [K % prime for prime in primes]

    # ----- Sieve stats
    unknowns_after_sieve = 1.0
    for prime in primes:
        unknowns_after_sieve *= (prime - 1) / prime

    prob_prime = 1 / (K_log + math.log(M))
    prob_prime_coprime = 1
    prob_prime_after_sieve = prob_prime / unknowns_after_sieve

    for prime in K_primes:
        if D % prime != 0:
            prob_prime_coprime *= (1 - 1/prime)

    count_coprime = SL-1
    for i in range(1, SL):
        for prime in K_primes:
            if (i % prime) == 0 and (D % prime) != 0:
                count_coprime -= 1
                break

    chance_coprime_composite = 1 - prob_prime / prob_prime_coprime
    prob_gap_shorter_hypothetical = chance_coprime_composite ** count_coprime

    # count_coprime already includes some parts of unknown_after_sieve
    print("\t{:.3f}% of sieve should be unknown ({}M) ~= {:.0f}".format(
        100 * unknowns_after_sieve,
        sieve_range//1e6,
        count_coprime * (unknowns_after_sieve / prob_prime_coprime)))
    print("\t{:.3f}% of {} digit numbers are prime".format(
        100 * prob_prime, K_digits))
    print("\t{:.3f}% of tests should be prime ({:.1f}x speedup)".format(
        100 * prob_prime_after_sieve, 1 / unknowns_after_sieve))
    print("\t~2x{:.1f} = {:.1}f PRP tests per m".format(
        1 / prob_prime_after_sieve, 2 / prob_prime_after_sieve))
    print("\tsieve_length={} is insufficient ~{:.2f}% of time".format(
        sieve_length, 100 * prob_gap_shorter_hypothetical))
    print()


    # ----- Main sieve loop.
    print("\nStarting m={}".format(M))
    print()

    # Used for various stats
    s_start_t = time.time()
    s_total_unknown = 0
    s_t_unk_low = 0
    s_t_unk_hgh = 0
    s_total_prp_tests = 0
    s_gap_out_of_sieve_prev = 0
    s_gap_out_of_sieve_next = 0
    s_best_merit_interval = 0
    s_best_merit_interval_m = 0

    for mi in range(M_inc):
        m = M + mi
        # TODO if gcd(m, d) != 1 continue?

        composite = [[], []]

        unknown_l = -1
        unknown_u = -1
        prev_p_i = 0
        next_p_i = 0

        # Read a line from the file
        line = unknown_file.readline()
        start, c_l, c_h = line.split("|")

        match = re.match(r"^([0-9]+) PRP -([0-9]+) to \+([0-9]+) : -([0-9]+) \+([0-9]+)", start)
        if match:
            mtest, prev_p_i, next_p_i, unknown_l, unknown_u = map(int, match.groups())
        else:
            match = re.match(r"^([0-9]+) : -([0-9]+) \+([0-9]+)", start)
            assert match, start
            mtest, unknown_l, unknown_u = map(int, match.groups())

        composite[0] = list(map(int,c_l.strip().split(" ")))
        composite[1] = list(map(int,c_h.strip().split(" ")))

        unknown_l_test = len(composite[0])
        unknown_u_test = len(composite[1])
        assert unknown_l == unknown_l_test, (unknown_l, unknown_l_test, "\t", start)
        assert unknown_u == unknown_u_test, (unknown_u, unknown_u_test, "\t", start)

        s_total_unknown += unknown_l + unknown_u
        s_t_unk_low += unknown_l
        s_t_unk_hgh += unknown_u

        # TODO break out to function, also count tests.
        if run_prp:
            center = m * K

            for i in composite[0]:
                s_total_prp_tests += 1;
                if gmpy2.is_prime(center + i):
                    prev_p_i = -i
                    break

            for i in composite[1]:
                s_total_prp_tests += 1;
                if gmpy2.is_prime(center + i):
                    next_p_i = i
                    break

            if next_p_i == 0:
                s_gap_out_of_sieve_next += 1
                # Using fallback to slower gmp routine
                next_p_i = gmpy2.next_prime(center + SL - 1) - center

            if prev_p_i == 0:
                s_gap_out_of_sieve_prev += 1
                # Medium ugly fallback.
                for i in range(SL+1):
                    composite = False
                    for prime, remain in zip(primes, remainder):
                        modulo = (remain * m) % prime
                        if i % prime == modulo:
                            composite = True
                            break
                    if not composite:
                        if gmpy2.is_prime(center - i):
                            prev_p_i = i
                            break

            gap = next_p_i + prev_p_i
            merit = gap / (K_log + math.log(m))
            if merit > min_merit:
                # TODO write to file.
                print("{}  {:.4f}  {} * {}#/{} -{} to +{}".format(
                    gap, merit, m, P, D, prev_p_i, next_p_i))

            if merit > s_best_merit_interval:
                s_best_merit_interval = merit
                s_best_merit_interval_m = m

        if mi in (1,10,100,500,1000, M_inc-1) or m % 5000 == 0:
            s_stop_t = time.time()
            secs = s_stop_t - s_start_t

            print("\t{:3d} {:4d} <- unknowns -> {:-4d}\t{:4d} <- gap -> {:-4d}".format(
                m,
                unknown_l, unknown_u,
                prev_p_i, next_p_i))
            if mi <= 10: continue

            # Stats!
            tests = mi + 1
            print("\t    tests     {:-10d} ({:.2f}/sec)  {:.0f} seconds elapsed".format(
                tests, tests / secs, secs))
            print("\t    unknowns  {:-10d} (avg: {:.2f}), {:.2f}% composite  {:.2f}% <- % -> {:.2f}%".format(
                s_total_unknown, s_total_unknown / tests,
                100 * (1 - s_total_unknown / (2 * (sieve_length - 1) * tests)),
                100 * s_t_unk_low / s_total_unknown,
                100 * s_t_unk_hgh / s_total_unknown))
            if run_prp:
                print("\t    prp tests {:-10d} (avg: {:.2f}) ({:.1f} tests/sec)".format(
                    s_total_prp_tests, s_total_prp_tests / tests, s_total_prp_tests / secs))
                print("\t    fallback prev_gap {} ({:.1f}%), next_gap {} ({:.1f}%)".format(
                    s_gap_out_of_sieve_prev, 100 * s_gap_out_of_sieve_prev / tests,
                    s_gap_out_of_sieve_next, 100 * s_gap_out_of_sieve_next / tests))
                print("\t    best merit this interval: {:.3f} (at m={})".format(
                    s_best_merit_interval, s_best_merit_interval_m))

            s_best_merit_interval = 0
            s_best_merit_interval_m = -1


prime_gap_test()
