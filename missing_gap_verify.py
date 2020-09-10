import gmpy2
import re
import time

failed = '''
	BOTH SIDES PRIME: 65309*8009#/1110 2160 130058  => gap = ???
	BOTH SIDES PRIME: 75061*8009#/1110 54212 71922  => gap = 28278
	BOTH SIDES PRIME: 78889*8009#/1110 67638 62614  => gap = 10852
	BOTH SIDES PRIME: 84647*8009#/1110 23398 105474 => gap = 58032
	BOTH SIDES PRIME: 38083*8009#/1110 12800 115314 => gap = 72638
	BOTH SIDES PRIME: 38821*8009#/1110 56418 73366  => gap = 56410
	BOTH SIDES PRIME: 82211*9001#/1110 61062 63962  => gap = 22316
	BOTH SIDES PRIME: 38407*9001#/1110 54452 71754  => gap = 84422
	BOTH SIDES PRIME: 37859*9001#/1110 11988 118292 => gap = 17108
	BOTH SIDES PRIME: 1207*10007#/1110 58508 73734  => gap = 71630
	BOTH SIDES PRIME: 1889*8009#/1110 162 131402    => gap = 73680
	BOTH SIDES PRIME: 1133*9001#/1110 60 126146     => gap = 55302
    BOTH SIDES PRIME: 23083*8009#/1110 25934 104964 => gap = 28598
    BOTH SIDES PRIME: 1027*9001#/1110 30720 93538   => gap = 70558
    BOTH SIDES PRIME: 8497*9001#/1110 7104 124834   => gap = 61390
    BOTH SIDES PRIME: 3497*10007#/1110 120 115574   => gap = 27482
    BOTH SIDES PRIME: 44321*8009#/1110 18750 105596 => gap = 54428
    BOTH SIDES PRIME: 16477*8009#/1110 40844 91362  => gap = 798
    BOTH SIDES PRIME: 7063*13001#/1110 19980 111974 => gap = 85406
    BOTH SIDES PRIME: 10163*8009#/1110 8880 123314  => gap = 8904
    BOTH SIDES PRIME: 3659*10007#/1110 96 117146    => gap = 31394
    BOTH SIDES PRIME: 32693*8009#/1110 39960 84146  => gap = 89418
    BOTH SIDES PRIME: 94483*8009#/1110 35520 97078  => gap = 82324
    BOTH SIDES PRIME: 20081*8009#/1110 41070 90806  => gap = 11972
    BOTH SIDES PRIME: 73481*14009#/2190 86962 43200 => gap = 121380
    BOTH SIDES PRIME: 31553*13001#/4110 51840 80804 (see below)
    BOTH SIDES PRIME: 31553*13001#/4110 51840 74714 => gap = 123194 (2000 away :/ )
    BOTH SIDES PRIME: 31099*8009#/1110 2048 130794  => gap = 2640
    BOTH SIDES PRIME: 37201*8009#/1110 26438 106068 => gap = 43020
    BOTH SIDES PRIME: 32941*8009#/1110 16442 113838 => gap = 52854
    BOTH SIDES PRIME: 65309*8009#/1110 2160 130058  => gap = 37262
    BOTH SIDES PRIME: 41117*14009#/1110 96766 25920 => gap = 59532 (1600 seconds)
    BOTH SIDES PRIME: 72949*8009#/1110 24000 102814 => gap = 75978
    BOTH SIDES PRIME: 42401*8009#/1110 10240 122358 => gap = 10528
    BOTH SIDES PRIME: 21679*8009#/1110 11100 117262 => gap = 43042
    BOTH SIDES PRIME: 15533*8009#/1110 21870 104684 => gap = 73388
    BOTH SIDES PRIME: 98491*8009#/1110 59834 68538  => gap = 4752
    BOTH SIDES PRIME: 13927*8009#/1110 240 126136    => gap = 59314
    BOTH SIDES PRIME: 54119*8009#/1110 35356 95442   => gap = 5910
    BOTH SIDES PRIME: 22249*8009#/1110 26198 97044   => gap = 54312
    BOTH SIDES PRIME: 53833*8009#/1110 5550 120424   => gap = 8214
    BOTH SIDES PRIME: 98501*8009#/1110 57298 72000   => gap = 61906
    BOTH SIDES PRIME: 78659*8009#/1110 900 131948    => gap = 20222
    BOTH SIDES PRIME: 91567*9001#/1110 24974 107574  => gap = 63492
    BOTH SIDES PRIME: 50707*9001#/1110 26244 90994   => gap = 46582
    BOTH SIDES PRIME: 26489*9001#/1110 19378 110406  => gap = 19398
    BOTH SIDES PRIME: 77039*9001#/1110 28174 96618   => gap = 9696
    BOTH SIDES PRIME: 15719*9001#/1110 32842 97200   => gap = 4344
    BOTH SIDES PRIME: 66451*9001#/1110 22478 103758  => gap = 82800
    BOTH SIDES PRIME: 86971*9001#/1110 51840 79042   => gap = 72958
    BOTH SIDES PRIME: 90037*9001#/1110 36506 85326   => gap = 47606
    BOTH SIDES PRIME: 58463*9001#/1110 25306 104976  => gap = 47928
    BOTH SIDES PRIME: 51473*9001#/1110 62908 68622   => gap = 25074
    BOTH SIDES PRIME: 39589*9001#/1110 38834 91446   => gap = 104000
    BOTH SIDES PRIME: 52823*9001#/1110 8748 123206   => gap = 52490
    BOTH SIDES PRIME: 84673*9001#/1110 74454 47836   => gap = 4936
    BOTH SIDES PRIME: 4673*10007#/1110 5184 118478   => gap = 8784
    BOTH SIDES PRIME: 8857*10007#/1110 64398 67138   => gap = 92332
    BOTH SIDES PRIME: 62411*13001#/1110 7290 118684  => gap = 13210
    BOTH SIDES PRIME: 57931*13001#/1110 1440 130124  => gap = 31982
    BOTH SIDES PRIME: 56087*13001#/1110 50342 72900  => gap = 42128
    BOTH SIDES PRIME: 4121*14009#/2190 100218 32000  => gap = 52736
    BOTH SIDES PRIME: 4609*13001#/4110 120858 2740   => gap = 23734
    BOTH SIDES PRIME: 98731*8009#/1110 36614 95262   => gap = 37586
    BOTH SIDES PRIME: 98759*8009#/1110 18742 107634  => gap = 25942
    BOTH SIDES PRIME: 4237*10007#/1110 37718 92562   => gap = 396
    BOTH SIDES PRIME: 72989*8009#/1110 24142 107988  => gap = 24910
    BOTH SIDES PRIME: 71179*9001#/1110 6660 123502   => gap = 29458
	BOTH SIDES PRIME: 7349*8009#/1110 56578 72294	 => gap = 56902
	BOTH SIDES PRIME: 43277*8009#/1110 20386 112452	 => gap = 38886
	BOTH SIDES PRIME: 67723*9001#/1110 46656 85132	 => gap = 70222
	BOTH SIDES PRIME: 80323*9001#/1110 77760 50926	 => gap = 5932
	BOTH SIDES PRIME: 76811*8009#/1110 162 131126	 => gap = 19424
	BOTH SIDES PRIME: 43763*9001#/1110 102958 29970	 => gap = 20244
	BOTH SIDES PRIME: 21823*9001#/1110 4800 124954	 => gap = 67762
	BOTH SIDES PRIME: 56417*8009#/1110 19980 112226	 => gap = 37022
'''

success = '''
    BOTH SIDES PRIME: 64003*12007#/1110 47954 82944 => gap 130898
'''

checks = '''
	BOTH SIDES PRIME: 27877*9001#/1110 57966 61618
'''

for test in checks.strip().split("\n"):
    m, p, d, l, h = map(int, re.search(r"(\d+).(\d+)..(\d+) (\d+) (\d+)", test).groups())
    N = m * gmpy2.primorial(p) // d
    low = N - l
    high = N + h

    print (f"Testing {m}*{p}#/{d} (-{l}, +{h})")
    t0 = time.time()
    assert gmpy2.is_prime(low)
    assert gmpy2.is_prime(high)
    t1 = time.time()

    print ("\tverified endpoints {:.2f} seconds".format(t1-t0))

    z = gmpy2.next_prime(low)
    t2 = time.time()

    print ("\t next_prime {}, {}   {:.1f} seconds".format(
        z == high, z - low, t2 - t1))
    print (f"\t{test.strip()}\t => gap = {z - low}")
    if z == high:
        print("\n"*3)
