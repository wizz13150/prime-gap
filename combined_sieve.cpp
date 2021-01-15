// Copyright 2020 Seth Troisi
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>
#include <cassert>
#include <chrono>
#include <clocale>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <functional>
#include <iostream>
#include <map>
#include <vector>

#include <gmp.h>
#include <primesieve.hpp>

#include "gap_common.h"
#include "modulo_search.h"

using std::cout;
using std::endl;
using std::pair;
using std::map;
using std::vector;
using namespace std::chrono;


/**
 * Two MACROS used to validate results
 * GMP_VALIDATE_FACTORS (validates all factors)
 * GMP_VALIDATE_LARGE_FACTORS (validate large factors)
 *
 * GMP_VALIDATE_LARGE_FACTORS only validates the rarer 60+ bit factors
 */

// Tweaking this doesn't seem to method1 much.
// method2 is more sensitive and set it's own.
#define SMALL_PRIME_LIMIT_METHOD1       400'000

// Compresses composite by 50-80%,
// Seems to be slightly slower for large p (> 15000?)
#define METHOD2_WHEEL   true

void set_defaults(struct Config& config);
void prime_gap_search(const struct Config& config);
void prime_gap_parallel(struct Config& config);


int main(int argc, char* argv[]) {
    Config config = Args::argparse(argc, argv);

    if (config.verbose >= 2) {
        printf("\tCompiled with GMP %d.%d.%d\n\n",
            __GNU_MP_VERSION, __GNU_MP_VERSION_MINOR, __GNU_MP_VERSION_PATCHLEVEL);
    }

    set_defaults(config);

    if (config.save_unknowns == 0) {
        cout << "Must set --save-unknowns" << endl;
        exit(1);
    }

    if (config.sieve_length < 6 * config.p || config.sieve_length > 22 * config.p) {
        int sl_low = ((config.p * 8 - 1) / 500 + 1) * 500;
        int sl_high = ((config.p * 20 - 1) / 500 + 1) * 500;
        printf("--sieve_length(%d) should be between [%d, %d]\n",
            config.sieve_length, sl_low, sl_high);
        exit(1);
    }

    if (config.valid == 0) {
        Args::show_usage(argv[0]);
        exit(1);
    }

    setlocale(LC_NUMERIC, "");
    if (config.verbose >= 0) {
        printf("\n");
        printf("Testing m * %u#/%u, m = %'ld + [0, %'ld)\n",
            config.p, config.d, config.mstart, config.minc);
    }
    setlocale(LC_NUMERIC, "C");

    #ifdef GMP_VALIDATE_FACTORS
    printf("\tValidating factors with GMP\n");
    #endif  // GMP_VALIDATE_FACTORS

    if (config.max_prime > 500'000'000) {
        float m_per = config.max_prime / ((float) config.minc * config.sieve_length);
        if (m_per < .1 && config.p <= 8000) {
            printf("\tmax_prime(%ldB) is probably too large\n",
                config.max_prime / 1'000'000'000);
        }
    }

    if (config.save_unknowns) {
        std::string fn = Args::gen_unknown_fn(config, ".txt");
        std::ifstream f(fn);
        if (f.good()) {
            printf("\nOutput file '%s' already exists\n", fn.c_str());
            exit(1);
        }
    }

    if (config.method1) {
        prime_gap_search(config);
    } else {
        prime_gap_parallel(config);
    }
}


void set_defaults(struct Config& config) {
    if (config.valid == 0) {
        // Don't do anything if argparse didn't work.
        return;
    }

    if (config.d % 4 == 0) {
        // AKA min-merit
        config.sieve_length = config.p * config.min_merit;

        // Start from 1
        config.mstart = 1;

        // Large prime near P to make D unique (chosen semi-randomly)
        config.d /= 4;
        vector<uint32_t> P_primes = get_sieve_primes(config.p);
        uint32_t rand_prime = P_primes[P_primes.size() - 2 - (rand() % 10)];
        uint32_t large_p = config.d > 1 ? config.d : rand_prime;
        assert(is_prime_brute(large_p));

        printf("d optimizer for P = %d# | large prime=%d | SL=%d (%.1f merit)\n",
                config.p, large_p, config.sieve_length, config.min_merit);

        /**
         * Secret value to optimize d
         * 1. Test small primorials to find optimal primorial
         * 2. Multiple by large prime (to make unique)
         * 3. test that ~same expected
         */
        vector<uint32_t> primes = {1,2,3,5,7,11,13,17,19,23};
        for (uint32_t lp : {1u, large_p}) {
            config.d = lp;
            for (uint32_t p : primes) {
                // check if large_p already includes p
                if (p != 1 && config.d % p == 0)
                    continue;

                if (__builtin_umul_overflow(config.d, p, &config.d)) {
                    // overflow
                    break;
                }

                // Try searching all values of m (up to 20,000)
                config.minc = std::min(config.d, 20'000U);
                auto expected = count_K_d(config);
                printf("Optimizing | d = %5d * %2d# | %d remaining, %5.0f avg gap | SL insufficient %.3f%% of time\n",
                    lp, p, std::get<1>(expected), std::get<0>(expected), 100 * std::get<2>(expected));
            }
        }
        exit(0);
    }

    mpz_t K;
    double K_log;
    {
        // Suppress log
        int temp = config.verbose;
        config.verbose = -1;

        int K_digits;
        K_stats(config, K, &K_digits, &K_log);

        config.verbose = temp;
    }

    if (config.sieve_length == 0) {
        // Change that a number near K is prime
        // GIVEN no factor of K or D => no factor of P#
        double N_log = K_log + log(config.mstart);
        double prob_prime_coprime_P = 1 / N_log - 1 / (N_log * N_log);

        // factors of K = P#/D
        vector<uint32_t> K_primes = get_sieve_primes(config.p);
        {
            // Adjust for prob_prime for no primes <= P
            for (auto prime : K_primes) {
                prob_prime_coprime_P /= (1 - 1.0 / prime);
            }

            // Remove any factors of D
            K_primes.erase(
                std::remove_if(K_primes.begin(), K_primes.end(),
                   [&](uint32_t p){ return config.d % p == 0; }),
                K_primes.end());
        }

        // K = #P/D
        // only numbers K+i has no factor <= p
        //      => (K+i, i) == (K, i) == 1
        //      => only relatively prime i's
        //
        // factors of d are hard because they depend on m*K
        //      some of these m are worse than others so use worst m

        assert( config.p >= 503 );

        // Search till chance of shorter gap is small.
        {
            // Code below is quite slow with larger values of d.
            assert( config.d <= 30030 );

            uint32_t base = mpz_fdiv_ui(K, config.d);

            // count of (m*K) % d over all m
            vector<uint32_t> count_by_mod_d(config.d, 0);
            {
                for (uint64_t mi = 0; mi < config.minc; mi++) {
                    uint64_t m = config.mstart + mi;
                    if (gcd(m, config.d) == 1) {
                        uint32_t center = (m * base) % config.d;
                        uint32_t center_down = (config.d - center) % config.d;

                        // distance heading up
                        count_by_mod_d[ center ] += 1;
                        // distance heading up
                        count_by_mod_d[ center_down ] += 1;
                    }
                }
            }

            // Note: By averaging over counts prob_larger could be improve here.
            map<uint32_t, uint32_t> coprime_by_mod_d;
            for (size_t i = 0; i < config.d; i++) {
                if (count_by_mod_d[i] > 0) {
                    coprime_by_mod_d[i] = 0;
                }
            }

            // Keep increasing SL till prob_gap_shorter < 0.8%
            for (size_t tSL = 1; ; tSL += 1) {
                bool any_divisible = false;
                for (int prime : K_primes) {
                    if ((tSL % prime) == 0) {
                        any_divisible = true;
                        break;
                    }
                }
                // Result will be the same as last.
                if (any_divisible) continue;

                // check if tSL is divisible for all center mods
                for (auto& coprime_counts : coprime_by_mod_d) {
                    const auto center = coprime_counts.first;
                    // Some multiple of d will mark this off (for these centers) don't count it.
                    if (gcd(center + tSL, config.d) == 1) {
                        coprime_counts.second += 1;
                    }
                }

                // Find the smallest number of coprimes
                uint32_t min_coprime = tSL;
                for (auto& coprime_counts : coprime_by_mod_d) {
                    min_coprime = std::min(min_coprime, coprime_counts.second);
                }

                // Assume each coprime is independent
                double prob_gap_shorter = pow(1 - prob_prime_coprime_P, min_coprime);

                // This seems to balance PRP fallback and sieve_size
                if (prob_gap_shorter <= 0.008) {
                    config.sieve_length = tSL;
                    printf("AUTO SET: sieve length: %ld (coprime: %d, prob_gap longer %.2f%%)\n",
                        tSL, min_coprime, 100 * prob_gap_shorter);
                    break;
                }
            }
        }
        assert( config.sieve_length > 100 ); // Something went wrong above.
    }

    if (config.max_prime == 0) {
        // each additional numbers removes unknowns / prime
        // and takes log2(prime / sieve_length) time

        // Not worth improving given method2 CTRL+C handling.
        if (K_log >= 1500) {
            config.max_prime =  100'000'000'000;
        } else {
            config.max_prime =   10'000'000'000;
        }
        if (config.method1) {
            printf("Can't use method1 and not set max_prime");
            exit(1);
        }
        if (config.verbose >= 0) {
            printf("AUTO SET: max_prime (log(K) = ~%.0f): %ld\n",
                K_log, config.max_prime);
            printf("WATCH for 'Estimated 2x faster (CTRL+C to stop sieving)' warning");
        }
    }

    mpz_clear(K);
}


static void insert_range_db(
        const struct Config& config,
        long num_rows,
        float time_sieve) {

    DB db_helper(config.search_db.c_str());
    sqlite3 *db = db_helper.get_db();

    const uint64_t rid = db_helper.config_hash(config);
    char sSQL[300];
    sprintf(sSQL,
        "INSERT INTO range(rid, P,D, m_start,m_inc,"
                          "sieve_length, max_prime,"
                          "min_merit,"
                          "num_m,"
                          "time_sieve)"
         "VALUES(%ld,  %d,%d, %ld,%ld,"
                "%d,%ld, %.3f,"
                "%ld,  %.2f)"
         "ON CONFLICT(rid) DO UPDATE SET time_sieve=%.2f",
            rid,  config.p, config.d, config.mstart, config.minc,
            config.sieve_length, config.max_prime,
            config.min_merit,
            num_rows,
            time_sieve, time_sieve);

    char *zErrMsg = nullptr;
    int rc = sqlite3_exec(db, sSQL, nullptr, nullptr, &zErrMsg);
    if (rc != SQLITE_OK) {
        printf("\nrange INSERT failed %d: %s\n",
            rc, sqlite3_errmsg(db));
        exit(1);
    }
}


// Method1


void save_unknowns_method1(
        std::ofstream &unknown_file,
        const uint64_t mi, int unknown_l, int unknown_u,
        const unsigned int SL, const vector<char> composite[]) {

    unknown_file << mi;
    unknown_file << " : -" << unknown_l << " +" << unknown_u << " |";

    for (int d = 0; d <= 1; d++) {
        char prefix = "-+"[d];

        for (size_t i = 1; i <= SL; i++) {
            if (!composite[d][i]) {
                unknown_file << " " << prefix << i;
            }
        }
        if (d == 0) {
            unknown_file << " |";
        }
    }
    unknown_file << "\n";
}


void prime_gap_search(const struct Config& config) {
    //const uint64_t P = config.p;
    const uint64_t D = config.d;
    const uint64_t M_start = config.mstart;
    const uint64_t M_inc = config.minc;

    const unsigned int SIEVE_LENGTH = config.sieve_length;
    const unsigned int SL = SIEVE_LENGTH;

    const uint64_t MAX_PRIME = config.max_prime;

    mpz_t test;
    mpz_init(test);

    if (config.verbose >= 2) {
        setlocale(LC_NUMERIC, "");
        printf("\n");
        printf("sieve_length: 2x %'d\n", config.sieve_length);
        printf("max_prime:       %'ld\n", MAX_PRIME);
        printf("\n");
        setlocale(LC_NUMERIC, "C");
    }

    // ----- Generate primes under SMALL_PRIME_LIMIT_METHOD1
    vector<uint32_t> small_primes;
    primesieve::generate_primes(SMALL_PRIME_LIMIT_METHOD1, &small_primes);

    // ----- Merit / Sieve stats
    mpz_t K;
    prob_prime_and_stats(config, K);


    // ----- Sieve stats
    const size_t SMALL_PRIME_PI = small_primes.size();
    {
        // deals with all primes that can mark off two items in SIEVE_LENGTH.
        assert( SMALL_PRIME_LIMIT_METHOD1 > 2 * SIEVE_LENGTH );
        if (config.verbose >= 1) {
            printf("\tUsing %'ld primes for SMALL_PRIME_LIMIT(%'d)\n\n",
                SMALL_PRIME_PI, SMALL_PRIME_LIMIT_METHOD1);
        }
        assert( small_primes[SMALL_PRIME_PI-1] < SMALL_PRIME_LIMIT_METHOD1);
        assert( small_primes[SMALL_PRIME_PI-1] + 200 > SMALL_PRIME_LIMIT_METHOD1);
    }

    const auto  s_setup_t = high_resolution_clock::now();

    // ----- Allocate memory for a handful of utility functions.

    // Remainders of (p#/d) mod prime
    typedef pair<uint64_t,uint64_t> p_and_r;
    vector<p_and_r> prime_and_remainder;
    prime_and_remainder.reserve(SMALL_PRIME_PI);

    // Big improvement over surround_prime is avoiding checking each large prime.
    // vector<m, vector<pi>> for large primes that only rarely divide a sieve
    int s_large_primes_rem = 0;

    double expected_primes_per = 0;

    // To save space, only save remainder for primes that divide ANY m in range.
    // This helps with memory usage when MAX_PRIME >> SL * MINC;
    auto *large_prime_queue = new vector<p_and_r>[M_inc];
    {
        size_t pr_pi = 0;
        if (config.verbose >= 0) {
            printf("\tCalculating first m each prime divides\n");
        }

        // large_prime_queue size can be approximated by
        // https://en.wikipedia.org/wiki/Meissel–Mertens_constant

        // Print "."s during, equal in length to 'Calculating ...'
        size_t print_dots = 38;

        const size_t expected_primes = primepi_estimate(MAX_PRIME);

        long first_m_sum = 0;

        if (config.verbose >= 0) {
            cout << "\t";
        }
        size_t pi = 0;

        primesieve::iterator it;
        for (uint64_t prime = it.next_prime(); prime <= MAX_PRIME; prime = it.next_prime()) {
            pi += 1;
            if (config.verbose >= 0 && (pi * print_dots) % expected_primes < print_dots) {
                cout << "." << std::flush;
            }

            // Big improvement over surround_prime is reusing this for each m.
            const uint64_t base_r = mpz_fdiv_ui(K, prime);

            if (prime <= SMALL_PRIME_LIMIT_METHOD1) {
                prime_and_remainder.emplace_back(prime, base_r);
                pr_pi += 1;
                continue;
            }

            expected_primes_per += (2.0 * SL + 1) / prime;

            // solve base_r * (M + mi) + (SL - 1)) % prime < 2 * SL
            //   0 <= (base_r * M + SL - 1) + base_r * mi < 2 * SL mod prime
            //
            // let shift = (base_r * M + SL - 1) % prime
            //   0 <= shift + base_r * mi < 2 * SL mod prime
            // add (prime - shift) to all three
            //
            //  (prime - shift) <= base_r * mi < (prime - shift) + 2 * SL mod prime
            uint64_t mi = modulo_search_euclid_gcd(
                    M_start, D, M_inc, SL, prime, base_r);

            // signals mi > M_inc
            if (mi == M_inc) continue;

            assert (mi < M_inc);

            // (M_start + mi) * last_prime < int64 (checked in argparse)
            uint64_t first = (base_r * (M_start + mi) + SL) % prime;
            assert( first <= 2*SL );

            //assert ( gcd(M + mi, D) == 1 );

            large_prime_queue[mi].emplace_back(prime, base_r);
            pr_pi += 1;

            s_large_primes_rem += 1;
            first_m_sum += mi;
        }
        if (config.verbose >= 0) {
            cout << endl;
        }

        assert(prime_and_remainder.size() == small_primes.size());
        if (config.verbose >= 1) {
            printf("\tSum of m1: %ld\n", first_m_sum);
            setlocale(LC_NUMERIC, "");
            if (pi == expected_primes) {
                printf("\tPrimePi(%ld) = %ld\n", MAX_PRIME, pi);
            } else {
                printf("\tPrimePi(%ld) = %ld guessed %ld\n", MAX_PRIME, pi, expected_primes);
            }

            printf("\t%ld primes not needed (%.1f%%)\n",
                (pi - SMALL_PRIME_PI) - pr_pi,
                100 - (100.0 * pr_pi / (pi - SMALL_PRIME_PI)));

            double mertens3 = log(log(MAX_PRIME)) - log(log(SMALL_PRIME_LIMIT_METHOD1));
            double theory_count = (2 * SL + 1) * mertens3;
            printf("\texpected large primes/m: %.1f (theoretical: %.1f)\n",
                expected_primes_per, theory_count);
            setlocale(LC_NUMERIC, "C");
        }
    }
    if (config.verbose >= 0) {
        auto  s_stop_t = high_resolution_clock::now();
        double   secs = duration<double>(s_stop_t - s_setup_t).count();
        printf("\n\tSetup took %.1f seconds\n", secs);
    }


    // ----- Open and Save to Output file
    std::ofstream unknown_file;
    if (config.save_unknowns) {
        std::string fn = Args::gen_unknown_fn(config, ".txt");
        printf("\nSaving to '%s'\n", fn.c_str());
        unknown_file.open(fn, std::ios::out);
        assert( unknown_file.is_open() ); // Can't open save_unknowns file
    }


    // ----- Main sieve loop.

    vector<char> composite[2] = {
        vector<char>(SIEVE_LENGTH+1, 0),
        vector<char>(SIEVE_LENGTH+1, 0)
    };
    assert( composite[0].size() == SIEVE_LENGTH+1 );
    assert( composite[1].size() == SIEVE_LENGTH+1 );

    // Used for various stats
    long  s_tests = 0;
    auto  s_start_t = high_resolution_clock::now();
    long  s_total_unknown = 0;
    long  s_t_unk_low = 0;
    long  s_t_unk_hgh = 0;
    long  s_large_primes_tested = 0;

    uint64_t last_mi = M_inc - 1;
    for (; last_mi > 0 && gcd(M_start + last_mi, D) > 1; last_mi -= 1);
    assert(last_mi >= 0 && last_mi < M_inc);
    assert(gcd(M_start + last_mi, D) == 1);

    for (uint64_t mi = 0; mi < M_inc; mi++) {
        const uint64_t m = M_start + mi;
        if (gcd(m, D) > 1) {
            assert( large_prime_queue[mi].empty() );
            continue;
        }

        // Reset sieve array to unknown.
        std::fill_n(composite[0].begin(), SIEVE_LENGTH+1, 0);
        std::fill_n(composite[1].begin(), SIEVE_LENGTH+1, 0);
        // center is always composite.
        composite[0][0] = composite[1][0] = 1;

        // For small primes that we don't do trick things with.
        for (const auto& pr : prime_and_remainder) {
            const uint64_t modulo = (pr.second * m) % pr.first;
            //            const auto& [prime, remainder] = prime_and_remainder[pi];
            //            const uint64_t modulo = (remainder * m) % prime;

            for (size_t x = modulo; x <= SIEVE_LENGTH; x += pr.first) {
                composite[0][x] = true;
            }

            // Not technically correct but fine to skip modulo == 0
            int first_negative = pr.first - modulo;
            assert(first_negative >= 0);
            for (size_t x = first_negative; x <= SIEVE_LENGTH; x += pr.first) {
                composite[1][x] = true;
            }
        }

        // Maybe useful for some stats later.
        // int unknown_small_l = std::count(composite[0].begin(), composite[0].end(), false);
        // int unknown_small_u = std::count(composite[1].begin(), composite[1].end(), false);

        for (const auto& pr : large_prime_queue[mi]) {
            s_large_primes_tested += 1;
            s_large_primes_rem -= 1;

            const auto& prime = pr.first;
            const auto& remainder = pr.second;

            // Large prime should divide some number in SIEVE for this m
            // When done find next mi where prime divides a number in SIEVE.
            const uint64_t modulo = (remainder * m) % prime;

            #ifdef GMP_VALIDATE_FACTORS
                mpz_mul_ui(test, K, m);
                assert(modulo == mpz_fdiv_ui(test, prime));
            #endif  // GMP_VALIDATE_FACTORS

            if (modulo <= SIEVE_LENGTH) {
                // Just past a multiple
                composite[0][modulo] = true;
            } else {
                // Don't have to deal with 0 case anymore.
                int64_t first_positive = prime - modulo;
                assert(first_positive <= SIEVE_LENGTH);  // Bad next m!
                // Just before a multiple
                composite[1][first_positive] = true;
            }

            // Find next mi where primes divides part of SIEVE
            {
                uint64_t start = mi + 1;
                uint64_t next_mi = start + modulo_search_euclid_gcd(
                        M_start + start, D, M_inc - start, SL, prime, remainder);
                if (next_mi == M_inc) continue;

                // (M_start + mi) * prime < int64 (checked in argparse)
                uint64_t mult = (remainder * (M_start + next_mi) + SL) % prime;
                assert(mult < (2 * SL + 1));

                //assert ( gcd(M_start + next_mi, D) == 1 );

                large_prime_queue[next_mi].push_back(pr);
                s_large_primes_rem += 1;
            }
        }
        large_prime_queue[mi].clear();
        large_prime_queue[mi].shrink_to_fit();

        s_tests += 1;

        // 2-3% of runtime, could be optimized into save_unknowns loop..
        int unknown_l = std::count(composite[0].begin(), composite[0].end(), false);
        int unknown_u = std::count(composite[1].begin(), composite[1].end(), false);
        s_total_unknown += unknown_l + unknown_u;
        s_t_unk_low += unknown_l;
        s_t_unk_hgh += unknown_u;

        // Save unknowns
        if (config.save_unknowns) {
            save_unknowns_method1(
                unknown_file,
                mi, unknown_l, unknown_u,
                SL, composite
            );
        }

        bool is_last = (mi == last_mi);

        if ((config.verbose + is_last >= 1) &&
                ((s_tests == 1 || s_tests == 10 || s_tests == 100 || s_tests == 500 || s_tests == 1000) ||
                 (s_tests % 5000 == 0) || is_last) ) {
            auto s_stop_t = high_resolution_clock::now();
            double   secs = duration<double>(s_stop_t - s_start_t).count();
            double t_secs = duration<double>(s_stop_t - s_setup_t).count();

            printf("\t%ld %4d <- unknowns -> %-4d\n",
                    m, unknown_l, unknown_u);

            if (config.verbose + is_last >= 1) {
                // Stats!
                printf("\t    intervals %-10ld (%.2f/sec, with setup per m: %.2g)  %.0f seconds elapsed\n",
                        s_tests, s_tests / secs, t_secs / s_tests, secs);
                printf("\t    unknowns  %-10ld (avg: %.2f), %.2f%% composite  %.2f <- %% -> %.2f%%\n",
                        s_total_unknown, s_total_unknown / ((double) s_tests),
                        100.0 * (1 - s_total_unknown / ((2.0 * SIEVE_LENGTH + 1) * s_tests)),
                        100.0 * s_t_unk_low / s_total_unknown,
                        100.0 * s_t_unk_hgh / s_total_unknown);
                printf("\t    large prime remaining: %d (avg/test: %ld)\n",
                        s_large_primes_rem, s_large_primes_tested / s_tests);
            }
        }
    }

    {
        double primes_per_m = s_large_primes_tested / s_tests;
        double error_percent = 100.0 * fabs(expected_primes_per - primes_per_m) /
            expected_primes_per;
        if (config.verbose >= 2 || error_percent > 0.5 ) {
            printf("\n");
            printf("Estimated primes/m error %.2f%%,\t%.1f vs expected %.1f\n",
                error_percent, primes_per_m, expected_primes_per);
        }
    }

    {
        auto s_stop_t = high_resolution_clock::now();
        double   secs = duration<double>(s_stop_t - s_setup_t).count();
        insert_range_db(config, s_tests, secs);
    }

    // Should be cleaning up after self.
    for(uint32_t mi = 0; mi < M_inc; mi++)  {
        assert( large_prime_queue[mi].empty() );
    }

    // ----- cleanup

    delete[] large_prime_queue;
    mpz_clear(K);
    mpz_clear(test);
}


// Method 2

void save_unknowns_method2(
        const struct Config& config,
        const vector<int32_t> &valid_mi,
        const vector<int32_t> &m_reindex,
        const vector<uint32_t> &i_reindex,
        const uint32_t reindex_m_wheel,
        const vector<uint32_t> *i_reindex_wheel,
        const vector<bool> *composite) {

    // ----- Open and Save to Output file
    std::ofstream unknown_file;
    {
        std::string fn = Args::gen_unknown_fn(config, ".txt");
        printf("\nSaving unknowns to '%s'\n", fn.c_str());
        unknown_file.open(fn, std::ios::out);
        assert( unknown_file.is_open() ); // Can't open save_unknowns file
    }

    const uint32_t M_start = config.mstart;
    const uint32_t D = config.d;
    const uint32_t SL = config.sieve_length;

    for (uint64_t mi : valid_mi) {
        uint64_t m = M_start + mi;
        assert(gcd(m, D) == 1);
        int32_t mii = m_reindex[mi];
        assert( mii >= 0 );

        const auto& comp = composite[mii];
        const vector<uint32_t> &i_reindex_m = reindex_m_wheel > 1 ?
            i_reindex_wheel[m % reindex_m_wheel] : i_reindex;
        assert(i_reindex_m.size() == 2 * SL + 1);

        //const size_t count_coprime_sieve = *std::max_element(i_reindex.begin(), i_reindex.end());
        //const size_t size_side = count_coprime_sieve / 2;
        // composite[0] isn't a real entry.
        //auto real_begin = comp.begin() + 1;
        //size_t unknown_l = std::count(real_begin, real_begin + size_side, false);
        //size_t unknown_u = std::count(real_begin + size_side, comp.end(), false);

        // XXX: Could be improved to std::count if it was know were i_reindex_m[i] == SL
        size_t unknown_l = 0;
        size_t unknown_u = 0;
        for (size_t i = 0; i < i_reindex_m.size(); i++) {
            if (!comp[i_reindex_m[i]]) {
                if (i <= SL) {
                    unknown_l++;
                } else {
                    unknown_u++;
                }
            }
        }

        unknown_file << mi << " : -" << unknown_l << " +" << unknown_u << " |";
        for (int d = 0; d <= 1; d++) {
            char prefix = "-+"[d];
            size_t found = 0;

            if (config.rle) {
                unknown_file << " ";
                int last = 0;
                for (size_t i = 1; i <= SL; i++) {
                    int a = SL + (2*d - 1) * i;
                    if (!comp[i_reindex_m[a]]) {
                        found += 1;

                        int delta = i - last;
                        last = i;

                        // Ascii 48 to 122 are all "safe" -> 75 characters -> 5625
                        // Not quite enough so we use 48 + 128 which includes
                        // non printable characters.
                        assert(0 <= delta && delta < (128L*128));
                        unsigned char upper = 48 + (delta / 128);
                        unsigned char lower = 48 + (delta % 128);
                        unknown_file << upper << lower;
                    }
                }
            } else {
                for (size_t i = 1; i <= SL; i++) {
                    int a = SL + (2*d - 1) * i;
                    if (!comp[i_reindex_m[a]]) {
                        unknown_file << " " << prefix << i;
                        found += 1;
                    }
                }
            }
            if (d == 0) {
                unknown_file << " |";
                assert( found == unknown_l );
            } else {
                assert( found == unknown_u );
            }
        }
        unknown_file << "\n";
    }
}


bool g_control_c = false;
void signal_callback_handler(int) {
    if (g_control_c) {
        cout << "Caught 2nd CTRL+C stopping now." << endl;
        exit(2);
    } else {
       cout << "Caught CTRL+C stopping and saving after next interval " << endl;
       g_control_c = true;
    }
}


class method2_stats {
    public:
        method2_stats(
                const struct Config& config,
                size_t valid_ms,
                uint64_t threshold,
                double initial_prob_prime
        ) {
            start_t = high_resolution_clock::now();
            interval_t = high_resolution_clock::now();
            total_unknowns = (2 * config.sieve_length + 1) * valid_ms;

            if (threshold <= 100000)
               next_mult = 10000;

            prob_prime = initial_prob_prime;
            current_prob_prime = prob_prime;
        }

        uint64_t  next_print = 0;
        uint64_t  next_mult = 100000;

        high_resolution_clock::time_point  start_t;
        high_resolution_clock::time_point  interval_t;

        long  total_unknowns;
        long  prime_factors = 0;
        long  small_prime_factors_interval = 0;
        long  large_prime_factors_interval = 0;

        size_t pi = 0;
        size_t pi_interval = 0;

        uint64_t  m_stops = 0;
        uint64_t  m_stops_interval = 0;

        uint64_t  validated_factors = 0;

        double prob_prime = 0;
        double current_prob_prime = 0;

};

void method2_increment_print(
        uint64_t prime,
        uint64_t LAST_PRIME,
        size_t valid_ms,
        double skipped_prp, double prp_time_est,
        vector<bool> *composite,
        method2_stats &stats,
        const struct Config& config
) {
        if (prime >= stats.next_print) {
            const size_t max_mult = 100'000'000'000;

            // 10, 20, 30, 40, 50, 100, 200, 300, 400, 500, 1000 ...
            // Print 60,70,80,90 billion because intervals are wider.
            size_t all_ten = prime > 1'000'000'000;
            size_t next_next_mult = (5 + 4 * all_ten) * stats.next_mult;
            if (next_next_mult <= max_mult && stats.next_print == next_next_mult) {
                stats.next_mult *= 10;
                stats.next_print = 0;
            }
            stats.next_print += stats.next_mult;
            stats.next_print = std::min(stats.next_print, LAST_PRIME);
        }

        auto   s_stop_t = high_resolution_clock::now();
        // total time, interval time
        double     secs = duration<double>(s_stop_t - stats.start_t).count();
        double int_secs = duration<double>(s_stop_t - stats.interval_t).count();

        uint32_t SIEVE_INTERVAL = 2 * config.sieve_length + 1;

        bool is_last = (prime == LAST_PRIME) || g_control_c;

        setlocale(LC_NUMERIC, "");
        if (config.verbose + is_last >= 1) {
            printf("%'-10ld (primes %'ld/%ld)\t(seconds: %.2f/%-.1f | per m: %.3g)",
                prime,
                stats.pi_interval, stats.pi,
                int_secs, secs,
                secs / valid_ms);
            if (int_secs > 240) {
                // Add " @ HH:MM:SS" so that it is easier to predict when the next print will happen
                time_t rawtime = std::time(nullptr);
                struct tm *tm = localtime( &rawtime );
                printf(" @ %d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
            }
            printf("\n");
            stats.interval_t = s_stop_t;
        }

        if ((config.verbose + 2*is_last + (prime > 1e9)) >= 2) {
            uint64_t t_total_unknowns = 0;
            for (size_t i = 0; i < valid_ms; i++) {
                t_total_unknowns += std::count(composite[i].begin(), composite[i].end(), false);
            }
            uint64_t new_composites = stats.total_unknowns - t_total_unknowns;

            printf("\tfactors  %'14ld \t"
                   "(interval: %'ld avg m/large_prime interval: %.1f)\n",
                stats.prime_factors,
                stats.small_prime_factors_interval + stats.large_prime_factors_interval,
                1.0 * stats.m_stops_interval / stats.pi_interval);
            // count_coprime_sieve * valid_ms also makes sense but leads to smaller numbers
            printf("\tunknowns %'9ld/%-5ld\t"
                   "(avg/m: %.2f) (composite: %.2f%% +%.3f%% +%'ld)\n",
                t_total_unknowns, valid_ms,
                1.0 * t_total_unknowns / valid_ms,
                100.0 - 100.0 * t_total_unknowns / (SIEVE_INTERVAL * valid_ms),
                100.0 * new_composites / (SIEVE_INTERVAL * valid_ms),
                new_composites);

            printf("\t~ 2x %.2f PRP/m\t\t"
                   "(~ %4.1f skipped PRP => %.1f PRP/seconds)\n",
                1 / stats.current_prob_prime, skipped_prp,
                skipped_prp / int_secs);
            if (stats.validated_factors) {
                printf("\tValidated %ld factors\n", stats.validated_factors);
            }

            double run_prp_mult = int_secs / (prp_time_est * skipped_prp);
            if (run_prp_mult > 2) {
                printf("\t\tEstimated ~%.1fx faster to just run PRP now (CTRL+C to stop sieving)\n",
                    run_prp_mult);
            }

            printf("\n");

            stats.pi += stats.pi_interval;
            stats.prime_factors += stats.small_prime_factors_interval;
            stats.prime_factors += stats.large_prime_factors_interval;
            stats.m_stops += stats.m_stops_interval;

            stats.total_unknowns = t_total_unknowns;

            stats.small_prime_factors_interval = 0;
            stats.large_prime_factors_interval = 0;
            stats.m_stops_interval = 0;
            stats.pi_interval = 0;
        }
        setlocale(LC_NUMERIC, "C");
}

void validate_factor_m_k_x(
        method2_stats& stats,
        mpz_t &test, mpz_t &K, int64_t m, uint32_t X,
        uint64_t prime, uint32_t SL) {
#ifdef GMP_VALIDATE_FACTORS
    stats.validated_factors += 1;
    mpz_mul_ui(test, K, m);
    mpz_sub_ui(test, test, SL);
    mpz_add_ui(test, test, X);
    uint64_t mod = mpz_fdiv_ui(test, prime);
    assert(mod == 0);
#endif  // GMP_VALIDATE_FACTORS
}

void method2_small_primes(const Config &config, const __mpz_struct *K,
                          method2_stats &stats,
                          const vector<int32_t> &valid_mi,
                          const vector<int32_t> &m_reindex, uint32_t reindex_m_wheel,
                          const vector<uint32_t> *i_reindex_wheel, const uint64_t SMALL_THRESHOLD,
                          const double prp_time_est, vector<bool> *composite) {
    method2_stats temp_stats(config, valid_mi.size(), SMALL_THRESHOLD, stats.prob_prime);
    const uint32_t P = config.p;
    const uint32_t D = config.d;

    const uint32_t SIEVE_LENGTH = config.sieve_length;
    uint32_t SIEVE_INTERVAL = 2 * SIEVE_LENGTH + 1;

    primesieve::iterator iter;
    uint64_t prime = 0;

    while (prime <= SMALL_THRESHOLD) {
        // Handle primes (+1) <= stats.next_mult

        std::vector<std::pair<uint32_t, uint32_t>> p_and_r;
        for (prime = iter.next_prime(); prime <= SMALL_THRESHOLD; prime = iter.next_prime()) {
            temp_stats.pi_interval += 1;

            // Handled by coprime_composite above
            if (D % prime != 0 && prime <= P)
                continue;

            if (reindex_m_wheel % prime == 0) {
                if (config.verbose >= 2) {
                    printf("\t%ld handled by coprime wheel(%d)\n", prime, reindex_m_wheel);
                }
                continue;
            }

            const uint32_t base_r = mpz_fdiv_ui(K, prime);
            p_and_r.push_back({(uint32_t) prime, base_r});

            if (prime >= temp_stats.next_print) {
                break;
            }
        }

        for (uint32_t mi : valid_mi) {
            int32_t mii = m_reindex[mi];
            assert(mii >= 0);

            uint64_t m = config.mstart + mi;
            const std::vector<uint32_t> &i_reindex_m = i_reindex_wheel[m % reindex_m_wheel];
            vector<bool> &composite_mii = composite[mii];

            bool centerOdd = ((D & 1) == 0) && (m & 1);
            bool lowIsEven = centerOdd == (SIEVE_LENGTH & 1);

            for (auto pr : p_and_r) {
                uint64_t a_prime = pr.first;
                uint64_t base_r = pr.second;
                // For each interval that prints

                uint64_t modulo = (base_r * m) % a_prime;

                // flip = (m * K - SL) % a_prime
                uint32_t flip = modulo + a_prime - ((SIEVE_LENGTH + 1) % a_prime);
                if (flip >= a_prime) flip -= a_prime;

                uint32_t first = a_prime - flip - 1;
                assert(first < a_prime );

                if (first < SIEVE_INTERVAL) {
                    uint32_t shift = a_prime;
                    if (a_prime > 2) {
                        bool evenFromLow = (first & 1) == 0;
                        bool firstIsEven = lowIsEven == evenFromLow;

#ifdef GMP_VALIDATE_FACTORS
                        validate_factor_m_k_x(temp_stats, test, K, M_start + mi, first, a_prime, SL);
                        assert( (mpz_even_p(test) > 0) == firstIsEven );
                        assert( mpz_odd_p(test) != firstIsEven );
#endif  // GMP_VALIDATE_FACTORS

                        if (firstIsEven) {
                            assert( (first >= SIEVE_INTERVAL) || composite_mii[i_reindex_m[first]] );

                            // divisible by 2 move to next multiple (an odd multiple)
                            first += a_prime;
                        }

                        // Don't need to count cross off even multiples.
                        shift *= 2;
                    }

                    for (size_t x = first; x < SIEVE_INTERVAL; x += shift) {
                        composite_mii[i_reindex_m[x]] = true;
                        temp_stats.small_prime_factors_interval += 1;
                    }
                }
            }
        }
        stats.small_prime_factors_interval += temp_stats.small_prime_factors_interval;
        stats.validated_factors += temp_stats.validated_factors;

        // Don't print final partial interval
        if (prime >= temp_stats.next_print) {
            // Calculated here with locals
            double prob_prime_after_sieve = stats.prob_prime * log(prime) * exp(GAMMA);
            // See THEORY.md
            double skipped_prp = 2 * valid_mi.size() * (1/stats.current_prob_prime - 1/prob_prime_after_sieve);
            stats.current_prob_prime = prob_prime_after_sieve;

            // Print counters & stats.
            method2_increment_print(
                    prime, config.max_prime,
                    valid_mi.size(),
                    skipped_prp, prp_time_est,
                    composite,
                    temp_stats, config);
        }
    }
}


// Would be nice to pass const but CTRL+C handler changes max_prime
void prime_gap_parallel(struct Config& config) {
    // Method2
    const uint32_t M_start = config.mstart;
    const uint32_t M_inc = config.minc;

    const uint32_t P = config.p;
    const uint32_t D = config.d;

    const uint32_t SIEVE_LENGTH = config.sieve_length;
    const uint32_t SL = SIEVE_LENGTH;
    // SIEVE_INTERVAL includes endpoints [-SL ... K ... SL]
    uint32_t SIEVE_INTERVAL = 2 * SIEVE_LENGTH + 1;

    const uint64_t MAX_PRIME = config.max_prime;

    mpz_t test, test2;
    mpz_init(test);
    mpz_init(test2);

    mpz_set_ui(test, MAX_PRIME);
    mpz_prevprime(test, test);

    uint64_t LAST_PRIME = mpz_get_ui(test);
    assert( LAST_PRIME <= MAX_PRIME && LAST_PRIME + 500 > MAX_PRIME);

    // ----- Generate primes for P
    const vector<uint32_t> P_primes = get_sieve_primes(P);
    assert( P_primes.back() == P);

    // ----- Sieve stats & Merit Stuff
    mpz_t K;
    const double K_log = prob_prime_and_stats(config, K);
    const double N_log = K_log + log(config.mstart);
    const double prob_prime = 1 / N_log - 1 / (N_log * N_log);


    // ----- Allocate memory

    vector<int32_t> valid_mi;
    vector<int32_t> m_reindex(M_inc, -1);
    vector<bool> m_not_coprime(M_inc, 1);
    {
        for (uint32_t mi = 0; mi < M_inc; mi++) {
            if (gcd(M_start + mi, D) == 1) {
                m_reindex[mi] = valid_mi.size();
                m_not_coprime[mi] = 0;
                valid_mi.push_back(mi);
            }
        }
    }
    const size_t valid_ms = valid_mi.size();

    // if [X] is coprime to K
    vector<char> coprime_composite(SIEVE_INTERVAL, 1);
    // reindex composite[m][X] for composite[m_reindex[m]][i_reindex[X]]
    vector<uint32_t> i_reindex(SIEVE_INTERVAL, 0);
    // which X are coprime to K (X has SIEVE_LENGTH added so x is positive)
    vector<int32_t> coprime_X;

    // reindex composite[m][i] using (m, wheel) (wheel is 1!,2!,3!,5!)
    // This could be first indexed by i_reindex,
    // Would reduce size from wheel * (2*SL+1) to wheel * coprime_i
#if METHOD2_WHEEL
    // Note: Larger wheel eliminates more numbers but takes more space.
    // 6 seems reasonable for larger numbers  (saves 2/3 memory)
    // 30 is maybe better for smaller numbers (saves 4/15 memory)
    uint32_t reindex_m_wheel = gcd(D, (SIEVE_INTERVAL < 80000) ? 30 : 6);

    vector<uint32_t> i_reindex_wheel[reindex_m_wheel];
    vector<size_t> i_reindex_wheel_count(reindex_m_wheel, 0);
#else
    uint32_t reindex_m_wheel = 1;
    vector<uint32_t> *i_reindex_wheel = &i_reindex;
    vector<size_t> i_reindex_wheel_count = {0, 0};
#endif  // METHOD2_WHEEL

    {
        for (uint32_t prime : P_primes) {
            if (D % prime != 0) {
                uint32_t first = SIEVE_LENGTH % prime;
                #ifdef GMP_VALIDATE_FACTORS
                    mpz_set(test, K);
                    mpz_sub_ui(test, test, SIEVE_LENGTH);
                    mpz_add_ui(test, test, first);
                    assert( 0 == mpz_fdiv_ui(test, prime) );
                #endif  // GMP_VALIDATE_FACTORS

                assert( 0 <= first && first < prime );
                assert( (SIEVE_LENGTH - first) % prime == 0 );

                for (size_t x = first; x < SIEVE_INTERVAL; x += prime) {
                    coprime_composite[x] = 0;
                }
            }
        }
        // Center should be marked composite by every prime.
        assert(coprime_composite[SL] == 0);
        {
            size_t coprime_count = 0;
            for (size_t X = 0; X < SIEVE_INTERVAL; X++) {
                if (coprime_composite[X] > 0) {
                    coprime_X.push_back(X);
                    coprime_count += 1;
                    i_reindex[X] = coprime_count;
                }
            }
            assert(coprime_count == coprime_X.size());
        }

#if METHOD2_WHEEL
        // Start at m_wheel == 0 so that re_index_m_wheel == 1 (D=1) works.
        for (size_t m_wheel = 0; m_wheel < reindex_m_wheel; m_wheel++) {
            if (gcd(reindex_m_wheel, m_wheel) > 1) continue;
            i_reindex_wheel[m_wheel].resize(SIEVE_INTERVAL, 0);

            // (m * K - SL) % wheel => (m_wheel - SL) % wheel
            uint32_t mod_center = m_wheel * mpz_fdiv_ui(K, reindex_m_wheel);
            uint32_t mod_low = (mod_center + reindex_m_wheel - (SL % reindex_m_wheel)) % reindex_m_wheel;

            size_t coprime_count = 0;
            for (size_t i = 0; i < SIEVE_INTERVAL; i++) {
                if (coprime_composite[i] > 0) {
                    if (gcd(mod_low + i, reindex_m_wheel) == 1) {
                        coprime_count += 1;
                        i_reindex_wheel[m_wheel][i] = coprime_count;
                    }
                }
            }
            i_reindex_wheel_count[m_wheel] = coprime_count;
        }
#else
        i_reindex_wheel_count[0] = i_reindex_wheel_count[1] = coprime_count;
#endif  // METHOD2_WHEEL
    }

    const size_t count_coprime_sieve = coprime_X.size();
    assert( count_coprime_sieve % 2 == 0 );

    const auto THRESHOLDS =
        calculate_thresholds_method2(config, count_coprime_sieve, valid_ms);
    const uint64_t SMALL_THRESHOLD = THRESHOLDS.first;
    const uint64_t MEDIUM_THRESHOLD = THRESHOLDS.second;
    if (config.verbose >= 1) {
        setlocale(LC_NUMERIC, "");
        printf("sieve_length:  2x %'d\n", config.sieve_length);
        printf("max_prime:        %'ld\n", config.max_prime);
        printf("small_threshold:  %'ld\n", SMALL_THRESHOLD);
        printf("middle_threshold: %'ld\n", MEDIUM_THRESHOLD);
        setlocale(LC_NUMERIC, "C");
    }

    // SMALL_THRESHOLD must handle all primes that can mark off two items in SIEVE_INTERVAL.
    assert( SMALL_THRESHOLD >= SIEVE_INTERVAL );
    assert( MEDIUM_THRESHOLD >= SMALL_THRESHOLD );
    assert( MEDIUM_THRESHOLD <= config.max_prime );

#if defined GMP_VALIDATE_LARGE_FACTORS && !defined GMP_VALIDATE_FACTORS
    // No overflow from gap_common.cpp checks
    const uint32_t M_end = M_start + M_inc;
    const uint64_t LARGE_PRIME_THRESHOLD = (1LL << 55) / M_end;
    if (LARGE_PRIME_THRESHOLD < LAST_PRIME && config.verbose >= 1) {
        printf("validating factors from primes > %ld\n", LARGE_PRIME_THRESHOLD);
    }
#endif

    // ----- Timing
    if (config.verbose >= 2) {
        printf("\n");
    }
    // Prints estimate of PRP/s
    const double prp_time_est = prp_time_estimate_composite(N_log, config.verbose);

    // Detailed timing info about different stages
    combined_sieve_method2_time_estimate(config, K, valid_ms, prp_time_est);


    /**
     * Much space is saved via a reindexing scheme
     * composite[mi][x] (0 <= mi < M_inc, -SL <= x <= SL) is re-indexed to
     *      composite[m_reindex[mi]][i_reindex[SL + x]]
     * m_reindex[mi] with (D, M + mi) > 0 are mapped to -1 (and must be handled by code)
     * i_reindex[x]  with (K, x) > 0 are mapped to 0 (and that bit is ignored)
     */

    // <char> is faster (0-5%?) than <bool>, but uses 8x the memory.
    // Need to change here and in `save_unknowns_method2` signature.
    vector<bool> *composite = new vector<bool>[valid_ms];
    {
        int align_print = 0;
        size_t guess = valid_ms * (count_coprime_sieve + 1) / 8 / 1024 / 1024;
        if (config.verbose >= 1) {
            align_print = printf("coprime m    %ld/%d,  ", valid_ms, M_inc);
            printf("coprime i     %ld/%d, ~%'ldMB\n",
                count_coprime_sieve / 2, SIEVE_LENGTH, guess);
        }

        if (reindex_m_wheel > 1) {
            // Update guess with first wheel count for OOM prevention check
            guess = valid_ms * (i_reindex_wheel_count[1] + 1) / 8 / 1024 / 1024;
        }

        // Try to prevent OOM, check composite < 7GB allocation,
        // combined_sieve seems to use ~5-20% extra space for i_reindex_wheel + extra
        assert(guess < 7 * 1024);

        size_t allocated = 0;
        for (size_t i = 0; i < valid_ms; i++) {
            int m_wheel = (M_start + valid_mi[i]) % reindex_m_wheel;
            assert(gcd(m_wheel, reindex_m_wheel) == 1);

            // +1 reserves extra 0th entry for i_reindex[x] = 0
            composite[i].resize(i_reindex_wheel_count[m_wheel] + 1, false);
            composite[i][0] = true;
            allocated += composite[i].size();
        }
        if (config.verbose >= 1 && reindex_m_wheel > 1) {
            printf("%*s", align_print, "");
            printf("coprime wheel %ld/%d, ~%'ldMB\n",
                allocated / (2 * valid_ms), SIEVE_LENGTH,
                allocated / 8 / 1024 / 1024);
        }

        if (config.verbose >= 1) {
            align_print += 1;  // avoid unused warning
            printf("\n");
        }
    }

    // Used for various stats
    method2_stats stats(config, valid_ms, SMALL_THRESHOLD, prob_prime);

    // For primes <= SMALL_THRESHOLD, handle per m (with better memory locality)
    // This makes it harder to print (see awkward inner loop)
    const size_t THREADS = 1;
    vector<int32_t> valid_mi_split[THREADS];
    for (size_t i = 0; i < valid_mi.size(); i++) {
        valid_mi_split[i * THREADS / valid_mi.size()].push_back(valid_mi[i]);
    }

    #pragma omp parallel for
    for (auto t : valid_mi_split) {
        cout << "Hi " << t.size() << endl;
        method2_small_primes(config, K, stats, t, m_reindex, reindex_m_wheel, i_reindex_wheel,
                             SMALL_THRESHOLD, prp_time_est, composite);
    }
    exit(1);

    primesieve::iterator it(SMALL_THRESHOLD);
    uint64_t prime = it.next_prime();
    assert(prime > SMALL_THRESHOLD);
    assert(SIEVE_INTERVAL < prime);

    const bool K_odd  = mpz_odd_p(K);
    const int K_mod3 = mpz_fdiv_ui(K, 3); // K % 3
    const int K_mod5 = mpz_fdiv_ui(K, 5); // K % 5
    const int K_mod7 = mpz_fdiv_ui(K, 7); // K % 7
    const int D_mod2 = D % 2 == 0;
    const int D_mod3 = D % 3 == 0;
    const int D_mod5 = D % 5 == 0;
    const int D_mod7 = D % 7 == 0;

    // Middle primes
    for (; prime <= MEDIUM_THRESHOLD; prime = it.next_prime()) {
        stats.pi_interval += 1;

        const uint64_t base_r = mpz_fdiv_ui(K, prime);
        mpz_set_ui(test, base_r);
        mpz_set_ui(test2, prime);
        assert(mpz_invert(test, test, test2) > 0);

        const int64_t inv_K = mpz_get_ui(test);
        assert((inv_K * base_r) % prime == 1);

        // -M_start % p
        const int32_t m_start_shift = (prime - (M_start % prime)) % prime;

        const bool M_X_parity = (M_start & 1) ^ (SIEVE_LENGTH & 1);

        /* Unoptimized expressive code

        - // (X & 1) == X_odd_test <-> ((X + SIEVE_LENGTH) % 2 == 1)
        - const bool X_odd_test = (SIEVE_LENGTH & 1) == 0;
        + const bool M_X_parity = (M_start & 1) ^ (SIEVE_LENGTH & 1);

        - // (m + M_start) * K = (X - SIEVE_LENGTH)
        - // m = (-X*K^-1 - M_start) % p = (X * -(K^-1) - M_start) % p
          int64_t mi_0 = ((prime - dist) * inv_K + m_start_shift) % prime;
        - assert( (base_r * (mi_0 + M_start) + dist) % prime == 0 );

        - / **
        -  * When K is odd
        -  * m parity (even/odd) must not match X parity
        -  * odd m * odd K + odd X   -> even
        -  * even m * odd K + even X -> even
        -  * /
        - size_t m_odd = (M_start + mi_0) & 1;
        - if (((X & 1) == X_odd_test) == m_odd) {
        + if (((X^mi_0) & M_start_X_odd_not_same_parity)) {

            stats.small_prime_factors_interval += 1;
        -    / *
        -    // Doesn't seem to help, slightly tested.
        -    if (D_mod3 && ((dist + K_mod3 * m) % 3 == 0))
        -        continue;
        -    if (D_mod5 && ((dist + K_mod5 * m) % 5 == 0))
        -        continue;
        -    if (D_mod7 && ((dist + K_mod7 * m) % 7 == 0))
        -        continue;
        -    // * /
        */

        // Find m*K = X, X in [L, R]
        for (int64_t X : coprime_X) {
            int32_t dist = X - SIEVE_LENGTH;
            int64_t mi_0 = ((prime - dist) * inv_K + m_start_shift) % prime;

            assert( K_odd || (dist&1) );

            uint32_t shift = (1 + K_odd) * prime;
            if (K_odd) {
                // Check if X parity == m parity
                if (((dist ^ mi_0) & 1) == M_X_parity) {
                    mi_0 += prime;
                }
            }

            // Seperate loop when shift > M_inc not significantly faster
            for (uint64_t mi = mi_0; mi < M_inc; mi += shift) {
                if (m_not_coprime[mi])
                    continue;

                size_t m = M_start + mi;
                int32_t mii = m_reindex[mi];
                assert(mii >= 0);

                stats.small_prime_factors_interval += 1;
#if METHOD2_WHEEL
                composite[mii][i_reindex_wheel[m % reindex_m_wheel][X]] = true;
#else
                // avoids trivial lookup + modulo
                composite[mii][i_reindex[X]] = true;
#endif  // METHOD2_WHEEL

#ifdef GMP_VALIDATE_FACTORS
                validate_factor_m_k_x(stats, test, K, M_start + mi, X, prime, SL);
                assert( mpz_odd_p(test) );
#endif  // GMP_VALIDATE_FACTORS
            }
        }

        if (prime >= stats.next_print) {
            // Calculated here with locals
            double prob_prime_after_sieve = prob_prime * log(prime) * exp(GAMMA);
            // See THEORY.md
            double skipped_prp = 2 * valid_ms * (1/stats.current_prob_prime - 1/prob_prime_after_sieve);
            stats.current_prob_prime = prob_prime_after_sieve;

            // Print counters & stats.
            method2_increment_print(
                prime, LAST_PRIME,
                valid_ms,
                skipped_prp, prp_time_est,
                composite,
                stats, config);
        }
    }


    // Setup CTRL+C catcher
    signal(SIGINT, signal_callback_handler);

    for (; prime <= MAX_PRIME; prime = it.next_prime()) {
        stats.pi_interval += 1;

        // Big improvement over surround_prime is reusing this for each m.
        const uint64_t base_r = mpz_fdiv_ui(K, prime);

        modulo_search_euclid_all_large(M_start, M_inc, SL, prime, base_r, [&](
                    uint32_t mi, uint64_t first) {
            assert (mi < M_inc);

            stats.m_stops_interval += 1;

            // With D even (K odd), (ms + mi) must be odd (or D and m will share a factor of 2)
            // Helps avoid wide memory read
            uint32_t m = M_start + mi;
            if (K_odd && ((m & 1) == 0)) {
                // assert(m_reindex[mi] < 0);
                return;
            }

            if (m_not_coprime[mi])
                return;

            // Returning first from modulo_search_euclid_all_small is
            // slightly faster on benchmarks, and slightly faster here

            // first = (SL - m * K) % prime
            //     Computed as
            // first =  2*SL - ((SL + m*K) % prime)
            //       =  SL - m * K
            //     Requires prime > 2*SL
            //uint64_t first = (base_r * (M_start + mi) + SL) % prime;
            assert( first <= 2*SL );
            first = 2*SL - first;

#ifdef GMP_VALIDATE_FACTORS
            {
#elif defined GMP_VALIDATE_LARGE_FACTORS
            if (prime > LARGE_PRIME_THRESHOLD) {
#else
            if (0) {
#endif
                validate_factor_m_k_x(stats, test, K, m, first, prime, SIEVE_LENGTH);
//                assert( mpz_odd_p(test) );
            }

            int64_t dist = first - SIEVE_LENGTH;
            if (D_mod2 && (dist & 1))
                return;
            if (D_mod3 && ((dist + K_mod3 * m) % 3 == 0))
                return;
            if (D_mod5 && ((dist + K_mod5 * m) % 5 == 0))
                return;
            if (D_mod7 && ((dist + K_mod7 * m) % 7 == 0))
                return;

            if (!coprime_composite[first]) {
                return;
            }

            int32_t mii = m_reindex[mi];
            assert( mii >= 0 );

            // if coprime with K, try to toggle off factor.
#if METHOD2_WHEEL
            composite[mii][i_reindex_wheel[m % reindex_m_wheel][first]] = true;
#else
            composite[mii][i_reindex[first]] = true;
#endif  // METHOD2_WHEEL
            stats.large_prime_factors_interval += 1;
        });

        if (prime >= stats.next_print) {
            // Calculated here with locals
            double prob_prime_after_sieve = prob_prime * log(prime) * exp(GAMMA);
            // See THEORY.md
            double skipped_prp = 2 * valid_ms * (1/stats.current_prob_prime - 1/prob_prime_after_sieve);
            stats.current_prob_prime = prob_prime_after_sieve;

            // Print counters & stats.
            method2_increment_print(
                prime, LAST_PRIME,
                valid_ms,
                skipped_prp, prp_time_est,
                composite,
                stats, config);

            // if is_last would truncate .max_prime by 1 million
            if (g_control_c && (prime != LAST_PRIME)) {
                // NOTE: the resulting files were sieved by 1 extra prime
                // they will differ from --max_prime=X in a few entries

                if (prime < 1'000'000) {
                    cout << "Exit(2) from CTRL+C @ prime=" << prime << endl;
                    exit(2);
                }

                cout << "Breaking loop from CTRL+C @ prime=" << prime << endl;
                // reset unknown_filename if cached;
                config.unknown_filename = "";
                config.max_prime = prime - (prime % 1'000'000);

                break;
            }

            #ifdef SAVE_INCREMENTS
            if (config.save_unknowns && prime > 1e10 && prime != LAST_PRIME) {
                // reset unknown_filename if cached;
                config.unknown_filename = "";
                uint64_t old = config.max_prime;
                config.max_prime = prime - (prime % 1'000'000);
                save_unknowns_method2(
                    config,
                    valid_mi, m_reindex, i_reindex,
                    reindex_m_wheel, i_reindex_wheel,
                    composite);
                config.max_prime = old;
            }
            #endif // SAVE_INCREMENTS
        }
    }

    // Likely zeroed in the last interval, but needed no printing
    stats.pi += stats.pi_interval;
    stats.prime_factors += stats.small_prime_factors_interval;
    stats.prime_factors += stats.large_prime_factors_interval;
    stats.m_stops += stats.m_stops_interval;

    {
        // See Merten's Third Theorem
        float expected_m_stops = (log(log(LAST_PRIME)) - log(log(MEDIUM_THRESHOLD))) * 2*SL * M_inc;
        float error_percent = (100.0 * fabs(expected_m_stops - stats.m_stops)) / expected_m_stops;
        if (config.verbose >= 3 || error_percent > 0.1 ) {
            printf("Estimated modulo searches (m/prime) error %.2f%%,\t%ld vs expected %.0f\n",
                error_percent, stats.m_stops, expected_m_stops);
        }
    }

    if (config.save_unknowns) {
        save_unknowns_method2(
            config,
            valid_mi, m_reindex, i_reindex,
            reindex_m_wheel, i_reindex_wheel,
            composite);

        auto s_stop_t = high_resolution_clock::now();
        double   secs = duration<double>(s_stop_t - stats.start_t).count();
        insert_range_db(config, valid_mi.size(), secs);
    }

    delete[] composite;

    mpz_clear(K);
    mpz_clear(test);
    mpz_clear(test2);
}

