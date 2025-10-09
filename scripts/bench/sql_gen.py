#!/usr/bin/env python3
"""
Storm ORM SQL Generation Performance Benchmark
Analyzes compile-time SQL generation and cache optimization
"""

import sys
import re
import subprocess
from pathlib import Path

# Add current directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))
from common import BenchmarkRunner, Colors


class SQLGenerationBenchmark(BenchmarkRunner):
    """SQL generation performance benchmark"""

    def __init__(self, binary_path='./build/debug/benchmarks/sql_generation_microbench'):
        super().__init__(binary_path)

    def parse_results(self, output):
        """Parse SQL generation benchmark output"""
        # Extract batch size performance data
        batch_data = []
        for line in output.split('\n'):
            # Match lines like: "        1 |             9.638 |    Likely |          ~58"
            match = re.match(r'\s*(\d+)\s*\|\s*([\d.]+)\s*\|\s*(\w+)\s*\|\s*~?(\d+)', line)
            if match:
                batch_data.append({
                    'size': int(match.group(1)),
                    'time': float(match.group(2)),
                    'cache_hit': match.group(3),
                    'sql_length': int(match.group(4))
                })

        # Extract cache effectiveness data
        cache_data = []
        lines = output.split('\n')
        for i, line in enumerate(lines):
            if 'Batch size' in line and 'iterations' in line:
                match = re.search(r'Batch size (\d+)', line)
                if match:
                    batch_size = int(match.group(1))
                    # Parse next lines for avg/min/max
                    if i + 3 < len(lines):
                        avg_match = re.search(r'Average:\s*([\d.]+)', lines[i + 1])
                        min_match = re.search(r'Min:\s*([\d.]+)', lines[i + 2])
                        max_match = re.search(r'Max:\s*([\d.]+)', lines[i + 3])
                        speedup_match = re.search(r'Speedup.*?:\s*([\d.]+)x', lines[i + 4])

                        if all([avg_match, min_match, max_match, speedup_match]):
                            cache_data.append({
                                'batch_size': batch_size,
                                'avg': float(avg_match.group(1)),
                                'min': float(min_match.group(1)),
                                'max': float(max_match.group(1)),
                                'speedup': float(speedup_match.group(1))
                            })

        return {'batch_data': batch_data, 'cache_data': cache_data}

    def display_results(self, data, **kwargs):
        """Display formatted SQL generation results"""

        print(f"\n{Colors.BOLD}{Colors.CYAN}╔════════════════════════════════════════════════════════════════════════════════╗{Colors.RESET}")
        print(f"{Colors.BOLD}{Colors.CYAN}║              SQL GENERATION MICRO-BENCHMARK RESULTS                           ║{Colors.RESET}")
        print(f"{Colors.BOLD}{Colors.CYAN}╚════════════════════════════════════════════════════════════════════════════════╝{Colors.RESET}\n")

        # Batch size performance table
        print(f"{Colors.BOLD}{Colors.BLUE}▶ BATCH SIZE PERFORMANCE ANALYSIS{Colors.RESET}")
        print("┌────────────┬─────────────────────┬─────────────────┬──────────────┐")
        print("│ Batch Size │ SQL Gen Time (μs)   │ Cache Status    │ SQL Length   │")
        print("├────────────┼─────────────────────┼─────────────────┼──────────────┤")

        for item in data['batch_data']:
            # Color code based on performance
            time_str = f"{item['time']:.3f}"
            if item['time'] < 50:
                colored_time = f"{Colors.GREEN}{time_str}{Colors.RESET}"
            elif item['time'] < 100:
                colored_time = f"{Colors.BLUE}{time_str}{Colors.RESET}"
            elif item['time'] < 200:
                colored_time = f"{Colors.YELLOW}{time_str}{Colors.RESET}"
            else:
                colored_time = f"{Colors.RED}{time_str}{Colors.RESET}"

            # Color code cache status
            if item['cache_hit'] == 'Likely':
                cache_status = f"{Colors.GREEN}✓ Cache Hit{Colors.RESET}"
            else:
                cache_status = f"{Colors.YELLOW}✗ Cache Miss{Colors.RESET}"

            # Highlight common batch sizes
            if item['size'] in [1, 10, 25, 50]:
                batch_str = f"{Colors.CYAN}{item['size']:>10}{Colors.RESET}"
            else:
                batch_str = f"{item['size']:>10}"

            print(f"│ {batch_str} │ {colored_time:>19} │ {cache_status:>15} │ {item['sql_length']:>12} │")

        print("└────────────┴─────────────────────┴─────────────────┴──────────────┘\n")

        # Cache effectiveness table
        if data['cache_data']:
            print(f"{Colors.BOLD}{Colors.BLUE}▶ CACHE EFFECTIVENESS TEST (100 iterations per batch size){Colors.RESET}")
            print("┌────────────┬──────────────┬──────────────┬──────────────┬──────────────┐")
            print("│ Batch Size │ Average (μs) │ Min (μs)     │ Max (μs)     │ Speedup      │")
            print("├────────────┼──────────────┼──────────────┼──────────────┼──────────────┤")

            for item in data['cache_data']:
                # Color code times
                avg_str = f"{item['avg']:.3f}"
                min_str = f"{item['min']:.3f}"
                max_str = f"{item['max']:.3f}"

                # Color speedup
                speedup_str = f"{item['speedup']:.1f}x"
                if item['speedup'] > 10:
                    colored_speedup = f"{Colors.GREEN}{speedup_str:>12}{Colors.RESET}"
                elif item['speedup'] > 5:
                    colored_speedup = f"{Colors.BLUE}{speedup_str:>12}{Colors.RESET}"
                elif item['speedup'] > 2:
                    colored_speedup = f"{Colors.YELLOW}{speedup_str:>12}{Colors.RESET}"
                else:
                    colored_speedup = f"{Colors.RED}{speedup_str:>12}{Colors.RESET}"

                batch_str = f"{Colors.CYAN}{item['batch_size']:>10}{Colors.RESET}"

                print(f"│ {batch_str} │ {avg_str:>12} │ {min_str:>12} │ {max_str:>12} │ {colored_speedup} │")

            print("└────────────┴──────────────┴──────────────┴──────────────┴──────────────┘\n")

        # Optimizations implemented
        print(f"{Colors.BOLD}{Colors.BLUE}▶ OPTIMIZATION IMPACT ANALYSIS{Colors.RESET}")
        print("┌──────────────────────────────────────────────────────────────────────────┐")
        print("│                     Key Optimizations Implemented                        │")
        print("├──────────────────────────────────────────────────────────────────────────┤")
        print(f"│ {Colors.GREEN}✓{Colors.RESET} Compile-time SQL prefix generation (ConstexprString)                  │")
        print(f"│ {Colors.GREEN}✓{Colors.RESET} Pre-computed field names and placeholders                             │")
        print(f"│ {Colors.GREEN}✓{Colors.RESET} Thread-local 8-entry cache with round-robin replacement               │")
        print(f"│ {Colors.GREEN}✓{Colors.RESET} Memory pre-allocation using exact size calculation                    │")
        print(f"│ {Colors.GREEN}✓{Colors.RESET} Value template reuse for bulk INSERT operations                       │")
        print("└──────────────────────────────────────────────────────────────────────────┘\n")

        # Performance legend
        print(f"{Colors.BOLD}{Colors.BLUE}▶ PERFORMANCE CHARACTERISTICS LEGEND{Colors.RESET}")
        print("┌──────────────────────────────────────────────────────────────────────────┐")
        print("│                         Time Color Coding                                │")
        print("├──────────────────────────────────────────────────────────────────────────┤")
        print(f"│ {Colors.GREEN}█████{Colors.RESET} < 50μs    : Excellent (likely cache hit)                          │")
        print(f"│ {Colors.BLUE}█████{Colors.RESET} 50-100μs  : Good (optimized generation)                           │")
        print(f"│ {Colors.YELLOW}█████{Colors.RESET} 100-200μs : Acceptable (cache miss, small batch)                 │")
        print(f"│ {Colors.RED}█████{Colors.RESET} > 200μs   : Slow (large batch generation)                         │")
        print("└──────────────────────────────────────────────────────────────────────────┘\n")


def main():
    """Main entry point"""
    import argparse

    parser = argparse.ArgumentParser(
        description='Storm ORM SQL Generation Performance Benchmark',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )

    parser.add_argument(
        '--binary',
        default='./build/debug/benchmarks/sql_generation_microbench',
        help='Path to benchmark binary (default: %(default)s)'
    )

    args = parser.parse_args()

    # Check if binary exists, offer to build if not
    binary_path = Path(args.binary)
    if not binary_path.exists():
        print(f"{Colors.YELLOW}Benchmark not found. Building it...{Colors.RESET}\n")

        try:
            subprocess.run(['cmake', '--preset', 'ninja-debug', '-DENABLE_BENCH=ON'], check=True)
            subprocess.run(['cmake', '--build', '--preset', 'ninja-debug', '--target', 'sql_generation_microbench'], check=True)
            print(f"{Colors.GREEN}Build completed successfully!{Colors.RESET}\n")
        except subprocess.CalledProcessError as e:
            print(f"{Colors.RED}Build failed: {e}{Colors.RESET}")
            sys.exit(1)

    # Run benchmark
    benchmark = SQLGenerationBenchmark(args.binary)
    benchmark.run()


if __name__ == '__main__':
    main()
