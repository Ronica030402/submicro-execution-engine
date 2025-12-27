# Latency Budget - Component-Level Breakdown

This document provides a detailed breakdown of the latency budget for on-server processing and end-to-end execution scenarios.

## 🕒 On-Server Processing (Latency Budget)

| Phase | Component | Latency | % of Total |
|-------|-----------|---------|------------|
| 1 | Network Ingestion | 850 ns | 31% |
| 2 | LOB Reconstruction | 250 ns | 9% |
| 3 | Deep OFI Calculation | 270 ns | 10% |
| 4 | Feature Engineering | 250 ns | 9% |
| 5 | Software Inference | 420 ns | 15% |
| 5 | **FPGA Inference** | **120 ns** | **5%** |
| 6 | Strategy Computation | 150 ns | 5% |
| 7 | Risk Checks | 60 ns | 2% |
| 8 | Smart Order Router | 120 ns | 4% |
| 9 | Order Submission | 380 ns | 14% |
| **TOTAL (Software)** | | **2,750 ns** | **2.75 μs** |
| **TOTAL (FPGA)** | | **2,450 ns** | **2.45 μs** |

## 🚀 End-to-End Latency (Including Network)

| Scenario | On-Server | Network | Total | Competitive? |
|----------|-----------|---------|-------|--------------|
| Development (Remote) | 2.75 μs | 500 μs | **~503 μs** | No |
| Co-located (Software) | 2.75 μs | 10 μs | **~13 μs** | Yes |
| Co-located (FPGA) | 2.45 μs | 10 μs | **~12.5 μs** | Yes |
| **Best-Case (Optimized)** | **2.20 μs** | **5 μs** | **~7.2 μs** | **Measured** |

**Key Insight:** Network location matters 100x more than on-server optimization. Co-location reduces latency from 500 μs to 10 μs (98% reduction).

---
*Last updated: December 2025*
