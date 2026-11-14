// Cost of packet_watch::Scope per client packet with FEATURE_METRICS (Phase 23, item 6).
//
// Reproduces the Scope's work with the OpenTelemetry SDK the metrics build uses: copy of the player
// name, three steady_clock reads, the ScopedLatency attribute map and one Record on a ~100-bucket
// histogram view (src/lib/metrics/metrics.cpp). It does not link Canary (the metrics preset folds
// the core into the executable). Build and run against the metrics preset's vcpkg tree:
//
//   V=build/linux-release-metrics/vcpkg_installed/x64-linux
//   g++ -std=c++20 -O2 -DNDEBUG tools/bench/packet_scope_bench.cpp -I$V/include -L$V/lib \
//       -Wl,--start-group $(ls $V/lib/libopentelemetry_*.a | sed 's#.*/lib\(.*\)\.a#-l\1#') -Wl,--end-group -lpthread -o /tmp/packet_scope_bench
//   /tmp/packet_scope_bench | grep 'ns per'
//
// 2026-11-14: 330-390 ns per packet (the exporter dump at exit is the histogram, not an error).
#include <opentelemetry/exporters/ostream/metric_exporter_factory.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/sdk/metrics/aggregation/histogram_aggregation.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/view/instrument_selector_factory.h>
#include <opentelemetry/sdk/metrics/view/meter_selector_factory.h>
#include <opentelemetry/sdk/metrics/view/view_factory.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace sdk = opentelemetry::sdk::metrics;
namespace api = opentelemetry::metrics;

int main() {
	auto provider = sdk::MeterProviderFactory::Create();
	auto* p = static_cast<sdk::MeterProvider*>(provider.get());
	sdk::PeriodicExportingMetricReaderOptions options;
	options.export_interval_millis = std::chrono::milliseconds(600000);
	options.export_timeout_millis = std::chrono::milliseconds(1000);
	p->AddMetricReader(sdk::PeriodicExportingMetricReaderFactory::Create(opentelemetry::exporter::metrics::OStreamMetricExporterFactory::Create(), options));
	auto config = std::make_shared<sdk::HistogramAggregationConfig>();
	for (double b = 0; b <= 100; b += 1) config->boundaries_.push_back(b);  // ~100 buckets like initHistograms
	config->boundaries_.push_back(std::numeric_limits<double>::infinity());
	p->AddView(sdk::InstrumentSelectorFactory::Create(sdk::InstrumentType::kHistogram, "method_latency", "us"),
	           sdk::MeterSelectorFactory::Create("performance", "1.0", ""),
	           sdk::ViewFactory::Create("method_latency", "Latency", sdk::AggregationType::kHistogram, config));
	api::Provider::SetMeterProvider(std::shared_ptr<api::MeterProvider>(std::move(provider)));
	auto meter = api::Provider::GetMeterProvider()->GetMeter("performance", "1.0");
	auto histogram = meter->CreateDoubleHistogram("method_latency", "Latency", "us");

	std::array<std::string, 256> labels;
	for (size_t i = 0; i < labels.size(); ++i) { char b[16]; std::snprintf(b, sizeof b, "packet_0x%02zX", i); labels[i] = b; }
	const std::string player = "Some Player Name Long";  // 21 chars: past the short-string buffer
	const opentelemetry::context::Context context;
	const int N = 2'000'000;
	volatile size_t sink = 0;
	for (int round = 0; round < 3; ++round) {
		auto start = std::chrono::steady_clock::now();
		for (int i = 0; i < N; ++i) {
			const uint8_t opcode = static_cast<uint8_t>(0x60 + (i % 40));
			std::string name = player;                                      // Scope::playerName
			auto scopeStart = std::chrono::steady_clock::now();            // Scope::start
			std::map<std::string, std::string> attrs { { "method", labels[opcode] } };  // ScopedLatency attrs
			auto begin = std::chrono::steady_clock::now();                 // ScopedLatency::begin
			auto end = std::chrono::steady_clock::now();                   // ScopedLatency::stop
			double elapsed = std::chrono::duration<double, std::micro>(end - begin).count();
			auto kv = opentelemetry::common::KeyValueIterableView<decltype(attrs)> { attrs };
			histogram->Record(elapsed, kv, context);
			sink += name.size() + static_cast<size_t>((end - scopeStart).count() & 1);
		}
		auto total = std::chrono::steady_clock::now() - start;
		std::printf("round %d: %.0f ns per packet scope\n", round, std::chrono::duration<double, std::nano>(total).count() / N);
	}
	return 0;
}
