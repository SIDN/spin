
Performance testing
--------------------------

This is a relatively simple performance testing procedure; we do not control much of the global environment in this setup, but it should at least give some indication as to how performance of SPIN is (when used with a Valibox), compared to other versions of SPIN.

Notes: these tests have not been performed in isolation; there may have been other factors at work, and other applications or devices requiring internet access and performing DNS queries. Keep this in mind when reading the numbers.

Steps per test:
1. Deploy version under test to a GL-Inet AR-150 device
2. Connect a laptop or computer
3. Log in to the device with SSH
4. Run top
5. Perform the test, while keeping an eye on memory and cpu usage
6. Note the test result on the device (like say, throughput, or latency)
7. Note the cpu and memory usage on the gl-inet

Tests:
1. IPv4 speed test: https://check4.sidnlabs.nl/marco/speedtest/example-gauges.html
2. IPv6 speed test: https://check6.sidnlabs.nl/marco/speedtest/example-gauges.html
3. Test website: nos.nl (in firefox, use network developer tool to measure load times, ctrl-shift-E)
4. Test website: youtube.com
5. dnsperf (https://www.dns-oarc.net/tools/dnsperf), met de file dns_perf_domains, afgekapt op 5000 entries (head -5000)

Systems under test:
A. Valibox with SPIN disabled
B. Valibox with SPIN current beta (kernel module)
C. Valibox with SPIN current dev (userspace)
D. Valibox with SPIN current dev (nflog for DNS)


1. IPv4 speed test

|  | CPU load | SPIN CPU % | SPIN memory % | Download speed (Mbps) | Upload speed (Mbps) | Ping (ms) | Jitter (ms)
|-|-|-|-|-| -| -| -|
| Valibox with SPIN disabled | 0.31 | N/A | N/A | 100.35 Mbps | 96.93 Mbps | 2.00 | 0.30
| Valibox with SPIN-beta (kernel) | 1.48 | 5% | 5% | 98.25 | 80.88 | 2.00 | 0.96
| Valibox with SPIN-dev (userspace) | 0.62 | 2% | 6% | 99.37 | 96.77 | 1.92 | 1.00
| Valibox with SPIN-dev (dns nflog) | 0.13 | 3% | 6% | 99.08 | 95.87 | 2.65 | 0.97

2. IPv6 speed test

|  | CPU load | SPIN CPU % | SPIN memory % | Download speed | Upload speed | Ping | Jitter
|-|-|-|-|-| -| -| -|
| Valibox with SPIN disabled | 0.16 | N/A | N/A | 98.96 Mbps | 94.23Mbps | 2.62 | 3.90
| Valibox with SPIN-beta (kernel) | 2.13 | 4% | 83.09 | 86.07 | 2.00 | 1.90
| Valibox with SPIN-dev (userspace) | 0.41 | 2% | 6% | 97.39 | 94.81 | 2.00 | 0.74
| Valibox with SPIN-dev (dns nflog) | 0.76 | 6% | 7% | 97.68 | 90.52 | 2.00 | 0.79

3. Test website: nos.nl

|  | CPU load | SPIN CPU % | SPIN memory % | Load time (avg) |
|-|-|-|-|-| -| -| -|
| Valibox with SPIN disabled | 0.04 | N/A | N/A | 1.64 |
| Valibox with SPIN-beta (kernel) | 1.80 | 12% | 5% | 1.68 |
| Valibox with SPIN-dev (userspace) | 0.61 | 6% | 6% | 1.87 |
| Valibox with SPIN-dev (dns nflog) | 0.84 | 5% | 7% | 1.76 |

4. Test website: youtube.com

|  | CPU load | SPIN CPU % | SPIN memory % | Load time (avg) |
|-|-|-|-|-| -| -| -|
| Valibox with SPIN disabled | 0.04 | N/A | N/A | 3.26 |
| Valibox with SPIN-beta (kernel) | 2.32 | 26% | 5% | 3.14 |
| Valibox with SPIN-dev (userspace) | 0.30 | 15% | 6% |  3.56 |
| Valibox with SPIN-dev (dns nflog) | 0.83 | 11% | 7% | 3.30 |

5. dnsperf (5000)

|  | CPU load | SPIN CPU % | SPIN memory % | Queries completed | Run time (s) |
|-|-|-|-|-| -| -| -|
| Valibox with SPIN disabled | 0.87 | N/A | N/A | 4946 (98.92%) |
| Valibox with SPIN-beta (kernel) | 2.86 | 28% | 8% | 1425 (28.50%) | 201.408382
| Valibox with SPIN-dev (userspace) | 
| Valibox with SPIN-dev (dns nflog) | 1.59 | 6% 7% | 4899 (97.98%) | 33.253010



