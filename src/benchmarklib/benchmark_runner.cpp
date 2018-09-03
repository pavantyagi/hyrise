#include <json.hpp>

#include <random>

#include "benchmark_runner.hpp"
#include "constant_mappings.hpp"
#include "import_export/csv_parser.hpp"
#include "planviz/lqp_visualizer.hpp"
#include "planviz/sql_query_plan_visualizer.hpp"
#include "scheduler/current_scheduler.hpp"
#include "sql/sql_pipeline_builder.hpp"
#include "storage/chunk_encoder.hpp"
#include "storage/storage_manager.hpp"
#include "tpch/tpch_db_generator.hpp"
#include "tpch/tpch_queries.hpp"
#include "utils/filesystem.hpp"
#include "utils/load_table.hpp"
#include "version.hpp"

namespace opossum {

BenchmarkRunner::BenchmarkRunner(const BenchmarkConfig& config, const NamedQueries& queries,
                                 const nlohmann::json& context)
    : _config(config), _queries(queries), _context(context) {
  // In non-verbose mode, disable performance warnings
  if (!config.verbose) {
    _performance_warning_disabler.emplace();
  }

  // Initialise the scheduler if the benchmark was requested to run multi-threaded
  if (config.enable_scheduler) {
    Topology::use_numa_topology(config.cores);
    config.out << "- Multi-threaded Topology:" << std::endl;
    Topology::get().print(config.out);

    const auto scheduler = std::make_shared<NodeQueueScheduler>();
    CurrentScheduler::set(scheduler);
  }
}

void BenchmarkRunner::run() {
  _config.out << "\n- Starting Benchmark..." << std::endl;

  auto benchmark_start = std::chrono::steady_clock::now();

  // Run the queries in the selected mode
  switch (_config.benchmark_mode) {
    case BenchmarkMode::IndividualQueries: {
      _benchmark_individual_queries();
      break;
    }
    case BenchmarkMode::PermutedQuerySets: {
      _benchmark_permuted_query_sets();
      break;
    }
  }

  auto benchmark_end = std::chrono::steady_clock::now();
  _total_run_duration = benchmark_end - benchmark_start;

  // Create report
  if (_config.output_file_path) {
    std::ofstream output_file(*_config.output_file_path);
    _create_report(output_file);
  } else {
    _create_report(std::cout);
  }

  // Visualize query plans
  if (_config.enable_visualization) {
    for (const auto& name_and_plans : _query_plans) {
      auto name = name_and_plans.first;
      boost::replace_all(name, " ", "_");
      const auto& lqps = name_and_plans.second.lqps;
      const auto& pqps = name_and_plans.second.pqps;

      GraphvizConfig graphviz_config;
      graphviz_config.format = "svg";

      for (auto lqp_idx = size_t{0}; lqp_idx < lqps.size(); ++lqp_idx) {
        const auto file_prefix = name + "-LQP-" + std::to_string(lqp_idx);
        LQPVisualizer{graphviz_config, {}, {}, {}}.visualize({lqps[lqp_idx]}, file_prefix + ".dot",
                                                             file_prefix + ".svg");
      }
      for (auto pqp_idx = size_t{0}; pqp_idx < pqps.size(); ++pqp_idx) {
        const auto file_prefix = name + "-PQP-" + std::to_string(pqp_idx);
        SQLQueryPlanVisualizer{graphviz_config, {}, {}, {}}.visualize(*pqps[pqp_idx], file_prefix + ".dot",
                                                                      file_prefix + ".svg");
      }
    }
  }
}

void BenchmarkRunner::_benchmark_permuted_query_sets() {
  // Init results
  for (const auto& named_query : _queries) {
    const auto& name = named_query.first;
    _query_results_by_query_name.emplace(name, QueryBenchmarkResult{});
  }

  auto mutable_named_queries = _queries;

  // For shuffling the query order
  std::random_device random_device;
  std::mt19937 random_generator(random_device());

  const auto number_of_queries_per_set = mutable_named_queries.size();
  auto currently_running_query_sets = std::atomic_uint{0};
  auto finished_query_set_runs = std::atomic_uint{0};
  auto finished_queries_total = std::atomic_uint{0};
  auto result_mutex = std::mutex{};

  auto tasks = std::vector<std::shared_ptr<AbstractTask>>{};
  auto state = BenchmarkState{_config.max_num_query_runs, _config.max_duration};

  while (state.keep_running(finished_query_set_runs.load(std::memory_order_relaxed))) {
    if (currently_running_query_sets.load(std::memory_order_relaxed) < _config.clients) {
      currently_running_query_sets++;
      std::shuffle(mutable_named_queries.begin(), mutable_named_queries.end(), random_generator);

      for (const auto& named_query : mutable_named_queries) {
        const auto query_run_begin = std::chrono::steady_clock::now();

        auto on_query_done = [query_run_begin, named_query, number_of_queries_per_set, &currently_running_query_sets,
                              &finished_query_set_runs, &finished_queries_total, &result_mutex, this]() {
          const auto duration = std::chrono::steady_clock::now() - query_run_begin;
          finished_queries_total++;
          if (finished_queries_total.load(std::memory_order_relaxed) % number_of_queries_per_set == 0) {
            currently_running_query_sets--;
            finished_query_set_runs++;
          }
          {
            auto lock = std::lock_guard<std::mutex>(result_mutex);
            auto& query_benchmark_result = _query_results_by_query_name[named_query.first];
            query_benchmark_result.duration += duration;
            query_benchmark_result.iteration_durations.push_back(duration);
            query_benchmark_result.num_iterations++;
          }
        };

        auto query_tasks = _schedule_or_execute_query(named_query, on_query_done);
        tasks.insert(tasks.end(), query_tasks.begin(), query_tasks.end());
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  // Wait for the rest of the tasks that didn't make it in time - they will not count toward the results
  // TODO(leander/anyone): To be replaced with something like CurrentScheduler::abort(),
  // that properly removes all remaining tasks from all queues, without having to wait for them
  CurrentScheduler::wait_for_tasks(tasks);
  DebugAssert(currently_running_query_sets.load(std::memory_order_relaxed) == 0,
              "All query set runs must be finished at this point");
}

void BenchmarkRunner::_benchmark_individual_queries() {
  for (const auto& named_query : _queries) {
    const auto& name = named_query.first;
    _config.out << "- Benchmarking Query " << name << std::endl;

    auto currently_running_queries = std::atomic_uint{0};
    auto finished_query_runs = std::atomic_uint{0};
    auto iteration_durations = std::vector<Duration>{};
    auto durations_mutex = std::mutex{};

    auto tasks = std::vector<std::shared_ptr<AbstractTask>>{};
    auto state = BenchmarkState{_config.max_num_query_runs, _config.max_duration};

    while (state.keep_running(finished_query_runs.load(std::memory_order_relaxed))) {
      if (currently_running_queries.load(std::memory_order_relaxed) < _config.clients) {
        currently_running_queries++;

        const auto query_run_begin = std::chrono::steady_clock::now();
        auto on_query_done = [query_run_begin, &currently_running_queries, &finished_query_runs, &iteration_durations,
                              &durations_mutex]() {
          const auto query_run_end = std::chrono::steady_clock::now();
          currently_running_queries--;
          finished_query_runs++;
          {
            auto lock = std::lock_guard<std::mutex>(durations_mutex);
            iteration_durations.push_back(query_run_end - query_run_begin);
          }
        };

        auto query_tasks = _schedule_or_execute_query(named_query, on_query_done);
        tasks.insert(tasks.end(), query_tasks.begin(), query_tasks.end());
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }
    auto result = QueryBenchmarkResult{};
    result.num_iterations = finished_query_runs.load(std::memory_order_relaxed);
    result.duration = state.benchmark_end - state.benchmark_begin;
    result.iteration_durations = iteration_durations;
    _query_results_by_query_name.emplace(name, result);

    // Wait for the rest of the tasks that didn't make it in time - they will not count toward the results
    // TODO(leander/anyone): To be replaced with something like CurrentScheduler::abort(),
    // that properly removes all remaining tasks from all queues, without having to wait for them
    CurrentScheduler::wait_for_tasks(tasks);
    DebugAssert(currently_running_queries.load(std::memory_order_relaxed) == 0,
                "All query runs must be finished at this point");
  }
}

std::vector<std::shared_ptr<AbstractTask>> BenchmarkRunner::_schedule_or_execute_query(
    const NamedQuery& named_query, const std::function<void()>& done_callback) {
  if (_config.enable_scheduler) {
    return _schedule_query(named_query, done_callback);
  }
  _execute_query(named_query, done_callback);
  return {};
}

std::vector<std::shared_ptr<AbstractTask>> BenchmarkRunner::_schedule_query(
    const NamedQuery& named_query, const std::function<void()>& done_callback) {
  const auto& name = named_query.first;
  const auto& sql = named_query.second;

  auto query_tasks = std::vector<std::shared_ptr<AbstractTask>>();

  auto pipeline_builder = SQLPipelineBuilder{sql}.with_mvcc(_config.use_mvcc);
  if (_config.enable_visualization) pipeline_builder.dont_cleanup_temporaries();
  auto pipeline = pipeline_builder.create_pipeline();

  auto tasks_per_statement = pipeline.get_tasks();
  tasks_per_statement.back().back()->set_done_callback(done_callback);

  for (auto tasks : tasks_per_statement) {
    CurrentScheduler::schedule_tasks(tasks);
    query_tasks.insert(query_tasks.end(), tasks.begin(), tasks.end());
  }

  // If necessary, keep plans for visualization
  if (_config.enable_visualization) {
    const auto query_plans_iter = _query_plans.find(name);
    if (query_plans_iter == _query_plans.end()) {
      QueryPlans plans{pipeline.get_optimized_logical_plans(), pipeline.get_query_plans()};
      _query_plans.emplace(name, plans);
    }
  }
  return query_tasks;
}

void BenchmarkRunner::_execute_query(const NamedQuery& named_query, const std::function<void()>& done_callback) {
  const auto& name = named_query.first;
  const auto& sql = named_query.second;

  auto pipeline_builder = SQLPipelineBuilder{sql}.with_mvcc(_config.use_mvcc);
  if (_config.enable_visualization) pipeline_builder.dont_cleanup_temporaries();
  auto pipeline = pipeline_builder.create_pipeline();
  // Execute the query, we don't care about the results
  pipeline.get_result_table();

  if (done_callback) done_callback();

  // If necessary, keep plans for visualization
  if (_config.enable_visualization) {
    const auto query_plans_iter = _query_plans.find(name);
    if (query_plans_iter == _query_plans.end()) {
      QueryPlans plans{pipeline.get_optimized_logical_plans(), pipeline.get_query_plans()};
      _query_plans.emplace(name, plans);
    }
  }
}

void BenchmarkRunner::_create_report(std::ostream& stream) const {
  nlohmann::json benchmarks;

  for (const auto& named_query : _queries) {
    const auto& name = named_query.first;
    const auto& query_result = _query_results_by_query_name.at(name);
    DebugAssert(query_result.iteration_durations.size() == query_result.num_iterations,
                "number of iterations and number of iteration durations does not match");

    const auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(query_result.duration).count();
    const auto duration_seconds = static_cast<float>(duration_ns) / 1'000'000'000;
    const auto items_per_second = static_cast<float>(query_result.num_iterations) / duration_seconds;
    const auto time_per_query = duration_ns / query_result.num_iterations;

    // Transform iteration Durations into numerical representation
    auto iteration_durations = std::vector<double>();
    iteration_durations.reserve(query_result.iteration_durations.size());
    std::transform(query_result.iteration_durations.cbegin(), query_result.iteration_durations.cend(),
                   std::back_inserter(iteration_durations), [](const auto& duration) {
                     return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
                   });

    nlohmann::json benchmark{
        {"name", name},
        {"iterations", query_result.num_iterations},
        {"iteration_durations", iteration_durations},
        {"avg_real_time_per_iteration", time_per_query},
        {"items_per_second", items_per_second},
        {"time_unit", "ns"},
    };

    benchmarks.push_back(benchmark);
  }

  const auto total_run_duration_seconds = std::chrono::duration_cast<std::chrono::seconds>(_total_run_duration).count();
  nlohmann::json report{
      {"context", _context}, {"benchmarks", benchmarks}, {"total_run_duration (s)", total_run_duration_seconds}};

  stream << std::setw(2) << report << std::endl;
}

BenchmarkRunner BenchmarkRunner::create(const BenchmarkConfig& config, const std::string& table_path,
                                        const std::string& query_path) {
  const auto tables = _read_table_folder(table_path);
  Assert(!tables.empty(), "No tables found in '" + table_path + "'");

  for (const auto& table_path_str : tables) {
    const auto table_name = filesystem::path{table_path_str}.stem().string();

    std::shared_ptr<Table> table;
    if (boost::algorithm::ends_with(table_path_str, ".tbl")) {
      table = load_table(table_path_str, config.chunk_size);
    } else {
      table = CsvParser{}.parse(table_path_str);
    }

    config.out << "- Adding table '" << table_name << "'" << std::endl;
    BenchmarkTableEncoder::encode(table_name, table, config.encoding_config);
    StorageManager::get().add_table(table_name, table);
  }

  const auto queries = _read_query_folder(query_path);
  return BenchmarkRunner(config, queries, create_context(config));
}

std::vector<std::string> BenchmarkRunner::_read_table_folder(const std::string& table_path) {
  const auto is_table_file = [](const std::string& filename) {
    return (boost::algorithm::ends_with(filename, ".csv") || boost::algorithm::ends_with(filename, ".tbl"));
  };

  filesystem::path path{table_path};
  Assert(filesystem::exists(path), "No such file or directory '" + table_path + "'");

  std::vector<std::string> tables;

  // If only one file was specified, add it and return
  if (filesystem::is_regular_file(path)) {
    Assert(is_table_file(table_path), "Specified file '" + table_path + "' is not a .csv or .tbl file");
    tables.push_back(table_path);
    return tables;
  }

  // Recursively walk through the specified directory and add all files on the way
  for (const auto& entry : filesystem::recursive_directory_iterator(path)) {
    const auto filename = entry.path().string();
    if (filesystem::is_regular_file(entry) && is_table_file(filename)) {
      tables.push_back(filename);
    }
  }

  return tables;
}

NamedQueries BenchmarkRunner::_read_query_folder(const std::string& query_path) {
  const auto is_sql_file = [](const std::string& filename) { return boost::algorithm::ends_with(filename, ".sql"); };

  filesystem::path path{query_path};
  Assert(filesystem::exists(path), "No such file or directory '" + query_path + "'");

  if (filesystem::is_regular_file(path)) {
    Assert(is_sql_file(query_path), "Specified file '" + query_path + "' is not an .sql file");
    return _parse_query_file(query_path);
  }

  // Recursively walk through the specified directory and add all files on the way
  NamedQueries queries;
  for (const auto& entry : filesystem::recursive_directory_iterator(path)) {
    const auto filename = entry.path().string();
    if (filesystem::is_regular_file(entry) && is_sql_file(filename)) {
      const auto file_queries = _parse_query_file(filename);
      queries.insert(queries.end(), file_queries.begin(), file_queries.end());
    }
  }

  return queries;
}

NamedQueries BenchmarkRunner::_parse_query_file(const std::string& query_path) {
  auto query_id = 0u;

  std::ifstream file(query_path);
  const auto filename = filesystem::path{query_path}.stem().string();

  NamedQueries queries;
  std::string query;
  while (std::getline(file, query)) {
    if (query.empty() || query.substr(0, 2) == "--") {
      continue;
    }

    const auto query_name = filename + '.' + std::to_string(query_id);
    queries.emplace_back(query_name, std::move(query));
    query_id++;
  }

  // More convenient names if there is only one query per file
  if (queries.size() == 1) {
    auto& query_name = queries[0].first;
    query_name.erase(query_name.end() - 2, query_name.end());  // -2 because .0 at end of name
  }

  return queries;
}

cxxopts::Options BenchmarkRunner::get_basic_cli_options(const std::string& benchmark_name) {
  cxxopts::Options cli_options{benchmark_name};

  // Make sure all current encoding types are shown
  std::vector<std::string> encoding_strings;
  encoding_strings.reserve(encoding_type_to_string.right.size());
  for (const auto& encoding : encoding_type_to_string.right) {
    encoding_strings.emplace_back(encoding.first);
  }

  const auto encoding_strings_option = boost::algorithm::join(encoding_strings, ", ");

  // Make sure all current compression types are shown
  std::vector<std::string> compression_strings;
  compression_strings.reserve(vector_compression_type_to_string.right.size());
  for (const auto& vector_compression : vector_compression_type_to_string.right) {
    compression_strings.emplace_back(vector_compression.first);
  }

  const auto compression_strings_option = boost::algorithm::join(compression_strings, ", ");

  // If you add a new option here, make sure to edit CLIConfigParser::basic_cli_options_to_json() so it contains the
  // newest options. Sadly, there is no way to to get all option keys to do this automatically.
  // clang-format off
  cli_options.add_options()
    ("help", "print this help message")
    ("v,verbose", "Print log messages", cxxopts::value<bool>()->default_value("false"))
    ("r,runs", "Maximum number of runs of a single query (set)", cxxopts::value<size_t>()->default_value("10000")) // NOLINT
    ("c,chunk_size", "ChunkSize, default is 2^32-1", cxxopts::value<ChunkOffset>()->default_value(std::to_string(Chunk::MAX_SIZE))) // NOLINT
    ("t,time", "Maximum seconds that a query (set) is run", cxxopts::value<size_t>()->default_value("60")) // NOLINT
    ("o,output", "File to output results to, don't specify for stdout", cxxopts::value<std::string>()->default_value("")) // NOLINT
    ("m,mode", "IndividualQueries or PermutedQuerySets, default is IndividualQueries", cxxopts::value<std::string>()->default_value("IndividualQueries")) // NOLINT
    ("e,encoding", "Specify Chunk encoding as a string or as a JSON config file (for more detailed configuration, see below). String options: " + encoding_strings_option, cxxopts::value<std::string>()->default_value("Dictionary"))  // NOLINT
    ("compression", "Specify vector compression as a string. Options: " + compression_strings_option, cxxopts::value<std::string>()->default_value(""))  // NOLINT
    ("scheduler", "Enable or disable the scheduler", cxxopts::value<bool>()->default_value("false")) // NOLINT
    ("cores", "Specify the number of cores used by the scheduler (if active). 0 means all available cores", cxxopts::value<uint>()->default_value("0")) // NOLINT
    ("clients", "Specify how many queries should run in parallel if the scheduler is active", cxxopts::value<uint>()->default_value("1")) // NOLINT
    ("mvcc", "Enable MVCC", cxxopts::value<bool>()->default_value("false")) // NOLINT
    ("visualize", "Create a visualization image of one LQP and PQP for each query", cxxopts::value<bool>()->default_value("false")); // NOLINT
  // clang-format on

  return cli_options;
}

nlohmann::json BenchmarkRunner::create_context(const BenchmarkConfig& config) {
  // Generate YY-MM-DD hh:mm::ss
  auto current_time = std::time(nullptr);
  auto local_time = *std::localtime(&current_time);
  std::stringstream timestamp_stream;
  timestamp_stream << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");

  return nlohmann::json{
      {"date", timestamp_stream.str()},
      {"chunk_size", config.chunk_size},
      {"build_type", IS_DEBUG ? "debug" : "release"},
      {"encoding", config.encoding_config.to_json()},
      {"benchmark_mode",
       config.benchmark_mode == BenchmarkMode::IndividualQueries ? "IndividualQueries" : "PermutedQuerySets"},
      {"max_runs", config.max_num_query_runs},
      {"max_duration (s)", std::chrono::duration_cast<std::chrono::seconds>(config.max_duration).count()},
      {"using_mvcc", config.use_mvcc == UseMvcc::Yes},
      {"using_visualization", config.enable_visualization},
      {"output_file_path", config.output_file_path ? *(config.output_file_path) : "stdout"},
      {"using_scheduler", config.enable_scheduler},
      {"cores", config.cores},
      {"clients", config.clients},
      {"verbose", config.verbose},
      {"GIT-HASH", GIT_HEAD_SHA1 + std::string(GIT_IS_DIRTY ? "-dirty" : "")}};
}

}  // namespace opossum
