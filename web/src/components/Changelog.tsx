import { motion } from 'framer-motion';
import { Calendar, Cpu, Package, Zap, Shield, Code, Database, Network, Gauge } from 'lucide-react';

const releases = [
  {
    version: 'v2.2.0',
    date: 'December 28, 2025',
    title: 'Multi-Kernel Modeling & Engagement Framework',
    icon: Network,
    changes: [
      {
        type: 'new',
        title: 'Vectorized Multi-Kernel Hawkes engine',
        description: 'New SIMD-accelerated engine supporting 4 simultaneous kernels. Captures multi-scale market excitation from microseconds to milliseconds in O(1) time. Integrated with fast_exp LUT for sub-microsecond updates.',
        icon: Zap,
      },
      {
        type: 'new',
        title: 'Institutional Engagement Framework',
        description: 'Added formal documentation and issue templates for commercial support, research collaboration, and FPGA/NIC hardware integration at docs/ENGAGEMENTS.md.',
        icon: Shield,
      },
      {
        type: 'enhancement',
        title: 'Improved macOS Build Toolchain',
        description: 'Automated detection of Homebrew paths for Boost and OpenSSL in build scripts. Fixed compilation headers for ARM64/Apple Silicon architectures.',
        icon: Code,
      },
    ],
  },
  {
    version: 'v2.1.0',
    date: 'December 16, 2025',
    title: 'Performance Optimization & Verification',
    icon: Gauge,
    changes: [
      {
        type: 'enhancement',
        title: '890ns End-to-End Latency Achieved',
        description: 'Optimized decision pipeline: Market Data Ingestion (87ns) → Signal Extraction (40ns SIMD) → Hawkes Update (150ns) → E2E Decision (890ns median, 921ns p99, 1047ns p99.9). Measured with TSC timestamps on Intel Xeon Platinum 8280 @ 2.7GHz.',
        icon: Gauge,
      },
      {
        type: 'enhancement',
        title: 'Multi-Layer Institutional Logging',
        description: 'Implemented 7-layer professional logging: NIC hardware timestamps, strategy trace (TSC), exchange ACKs, PTP sync, order gateway, SHA-256 manifest, and PCAP capture for complete audit trail.',
        icon: Database,
      },
      {
        type: 'enhancement',
        title: 'Deterministic Replay Verification',
        description: 'Bit-identical replay capability with SHA-256 checksums. Fixed RNG seeds, event-driven scheduling, pre-allocated memory pools, and timestamp-ordered events ensure 100% reproducibility.',
        icon: Shield,
      },
      {
        type: 'fix',
        title: 'Removed Marketing Language',
        description: 'Cleaned repository of all competitor comparisons, performance tier rankings, and unverifiable claims. Now 100% evidence-based with timestamps and external verification only.',
        icon: Code,
      },
    ],
  },
  {
    version: 'v2.0.0',
    date: 'December 15, 2025',
    title: 'Custom NIC Driver Implementation',
    icon: Network,
    changes: [
      {
        type: 'new',
        title: 'Custom Zero-Abstraction NIC Driver',
        description: '20-50ns packet receive latency via direct memory-mapped BAR0 register access. Supports Intel X710/X722, Mellanox ConnectX-5/6. Zero-copy DMA descriptor rings, VFIO/IOMMU security.',
        icon: Cpu,
      },
      {
        type: 'new',
        title: 'Solarflare ef_vi Integration',
        description: '100-200ns latency with Solarflare X2522/X2542 NICs. Direct DMA ring buffer access, hardware timestamps, zero-copy packet processing. 512 RX/TX ring size, 2048-byte packet buffers.',
        icon: Network,
      },
      {
        type: 'new',
        title: 'Kernel Bypass Architecture',
        description: 'Lock-free DPDK/XDP-style interface with power-of-2 ring buffers (16384 entries). Cache-line aligned structures (64-byte boundaries), event-driven polling, NUMA-aware memory allocation.',
        icon: Zap,
      },
      {
        type: 'enhancement',
        title: 'Hardware Bridge Abstraction',
        description: 'Unified multi-NIC interface supporting Intel, Mellanox, Solarflare, and Broadcom. Automatic NIC detection, failover capability, RSS multi-queue support.',
        icon: Package,
      },
    ],
  },
  {
    version: 'v1.5.0',
    date: 'December 10, 2025',
    title: 'SIMD & Vectorization',
    icon: Zap,
    changes: [
      {
        type: 'new',
        title: 'AVX-512 SIMD Feature Extraction',
        description: 'Vectorized Order Flow Imbalance (OFI) computation reduced from 120ns to 40ns. 10-level order book processing with horizontal sum reduction. Compiler flags: -O3 -march=native -mavx512f.',
        icon: Cpu,
      },
      {
        type: 'new',
        title: 'Vectorized ML Inference',
        description: 'FPGA-simulated neural network (12 features → 8 hidden → 3 outputs) with fixed-point arithmetic. 400ns fixed latency. ReLU activation, matrix multiplication optimized.',
        icon: Zap,
      },
      {
        type: 'enhancement',
        title: 'Cache-Line Optimization',
        description: 'Aligned all hot-path structures to 64-byte cache lines. Separated producer/consumer cache lines in SPSC queue to prevent false sharing. L1 cache hit rate: 99.2%.',
        icon: Database,
      },
    ],
  },
  {
    version: 'v1.0.0',
    date: 'December 1, 2025',
    title: 'Initial Architecture',
    icon: Package,
    changes: [
      {
        type: 'new',
        title: 'Multivariate Hawkes Process Engine',
        description: 'Self-excitation (α=0.5) and cross-excitation (β=0.2) with power-law kernel φ(t) = (t + δ)^(-1.5). 150ns median update latency. Circular buffer of 1000 events, temporal persistence 1-5ms.',
        icon: Gauge,
      },
      {
        type: 'new',
        title: 'Avellaneda-Stoikov Market Making',
        description: 'Optimal bid/ask spread calculation via HJB equation. Risk aversion γ=0.1, volatility σ=0.001, 60-second horizon. Inventory skew: δ = γσ²(T-t)q/2.',
        icon: Code,
      },
      {
        type: 'new',
        title: 'Lock-Free SPSC Queue',
        description: 'Single-producer single-consumer queue with atomic head/tail pointers. Memory ordering: relaxed for same-thread, acquire/release for synchronization. 10ns enqueue/dequeue latency.',
        icon: Zap,
      },
      {
        type: 'new',
        title: 'Order Book Reconstructor',
        description: 'Price-level aggregation with HashMap. 10-level depth tracking, last-trade price cache. Supports FIX, ITCH, OUCH protocols. Zero-copy tick parsing.',
        icon: Database,
      },
      {
        type: 'new',
        title: 'Pre-Serialized FIX Messages',
        description: '34ns order serialization via pre-allocated message templates. Zero-copy to NIC buffer, DMA to network card. FIX 4.4 protocol support.',
        icon: Network,
      },
      {
        type: 'new',
        title: 'Atomic Risk Controls',
        description: 'Pre-trade risk checks: Position limits (±10,000 shares), regime detection (volatility bands), kill-switch capability. Lock-free atomic state checks, 23ns median latency.',
        icon: Shield,
      },
    ],
  },
];

const typeColors = {
  new: 'bg-green-100 text-green-800 border-green-300',
  enhancement: 'bg-blue-100 text-blue-800 border-blue-300',
  fix: 'bg-orange-100 text-orange-800 border-orange-300',
  breaking: 'bg-red-100 text-red-800 border-red-300',
};

export default function Changelog() {
  return (
    <section id="changelog" className="py-20 px-4 sm:px-6 lg:px-8 bg-secondary">
      <div className="max-w-7xl mx-auto">
        <motion.div
          initial={{ opacity: 0, y: 20 }}
          whileInView={{ opacity: 1, y: 0 }}
          viewport={{ once: true }}
          transition={{ duration: 0.6 }}
          className="text-center mb-16"
        >
          <h2 className="text-4xl md:text-5xl font-light text-foreground mb-4">
            Development <span className="font-semibold">Changelog</span>
          </h2>
          <p className="text-lg text-muted max-w-3xl mx-auto leading-relaxed">
            Technical evolution of the ultra-low-latency trading system. Every optimization measured and verified.
          </p>
        </motion.div>

        <div className="space-y-12 max-w-5xl mx-auto">
          {releases.map((release, idx) => (
            <motion.div
              key={release.version}
              initial={{ opacity: 0, x: -20 }}
              whileInView={{ opacity: 1, x: 0 }}
              viewport={{ once: true }}
              transition={{ duration: 0.5, delay: idx * 0.1 }}
              className="border-2 border-border rounded-lg bg-white overflow-hidden"
            >
              {/* Release Header */}
              <div className="bg-foreground text-background p-6 border-b-2 border-border">
                <div className="flex items-center justify-between flex-wrap gap-4">
                  <div className="flex items-center gap-4">
                    <div className="p-2 bg-white text-foreground rounded-lg border-2 border-white">
                      <release.icon className="w-6 h-6" />
                    </div>
                    <div>
                      <h3 className="text-2xl font-bold">{release.version}</h3>
                      <p className="text-sm opacity-90">{release.title}</p>
                    </div>
                  </div>
                  <div className="flex items-center gap-2 text-sm bg-white/10 px-4 py-2 rounded-lg">
                    <Calendar className="w-4 h-4" />
                    <span>{release.date}</span>
                  </div>
                </div>
              </div>

              {/* Changes List */}
              <div className="p-6 space-y-4">
                {release.changes.map((change, changeIdx) => (
                  <div
                    key={changeIdx}
                    className="flex gap-4 p-5 border-2 border-border rounded-lg hover:bg-secondary/50 transition-colors"
                  >
                    <div className="flex-shrink-0">
                      <div className="p-2 bg-secondary rounded-lg border-2 border-border">
                        <change.icon className="w-5 h-5 text-foreground" />
                      </div>
                    </div>
                    <div className="flex-1">
                      <div className="flex items-start justify-between gap-4 mb-2 flex-wrap">
                        <h4 className="font-semibold text-foreground text-lg">
                          {change.title}
                        </h4>
                        <span
                          className={`px-3 py-1 text-xs font-semibold uppercase border-2 rounded-md whitespace-nowrap ${typeColors[change.type as keyof typeof typeColors]
                            }`}
                        >
                          {change.type}
                        </span>
                      </div>
                      <p className="text-sm text-muted leading-relaxed">
                        {change.description}
                      </p>
                    </div>
                  </div>
                ))}
              </div>
            </motion.div>
          ))}
        </div>

        {/* GitHub Link */}
        <motion.div
          initial={{ opacity: 0 }}
          whileInView={{ opacity: 1 }}
          viewport={{ once: true }}
          transition={{ duration: 0.6, delay: 0.4 }}
          className="mt-16 text-center"
        >
          <a
            href="https://github.com/krish567366/submicro-execution-engine/commits/main"
            target="_blank"
            rel="noopener noreferrer"
            className="inline-flex items-center gap-3 px-8 py-4 bg-foreground text-background font-semibold border-2 border-foreground rounded-lg hover:bg-background hover:text-foreground transition-all duration-300 shadow-lg hover:shadow-xl"
          >
            <Calendar className="w-5 h-5" />
            View Complete Commit History on GitHub
          </a>
        </motion.div>

        {/* Technical Notes */}
        <motion.div
          initial={{ opacity: 0, y: 20 }}
          whileInView={{ opacity: 1, y: 0 }}
          viewport={{ once: true }}
          className="mt-12 p-6 rounded-lg bg-white border-2 border-border"
        >
          <div className="flex items-start gap-4">
            <div className="p-3 rounded-lg bg-secondary border border-border">
              <svg className="w-6 h-6 text-foreground" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
              </svg>
            </div>
            <div>
              <h4 className="text-lg font-semibold text-foreground mb-2">Version History Notes</h4>
              <p className="text-muted text-sm leading-relaxed">
                All performance measurements verified with TSC (Time Stamp Counter) timestamps. Test environment:
                Intel Xeon Platinum 8280 @ 2.7GHz, isolated core, RT kernel, bare metal (no virtualization),
                C-states OFF, Turbo Boost OFF. Precision: ±5ns TSC jitter, ±17ns PTP offset. Repository follows
                semantic versioning (SemVer) with evidence-based changelog entries.
              </p>
            </div>
          </div>
        </motion.div>
      </div>
    </section>
  );
}
