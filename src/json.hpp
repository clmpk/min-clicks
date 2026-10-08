#pragma once

#include <glaze/glaze.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include "board.hpp"

namespace minclicks {

using SingleActions = std::vector<ExpandedAction>;

struct AllActions {
  SingleActions optimal;
  SingleActions way8;  // Fixed: Changed from '8way' to a legal identifier
  SingleActions lzini;
  SingleActions hzini;
};

using FlexibleActions = std::variant<SingleActions, AllActions>;

struct RootDocument {
  std::string profile_name;
  FlexibleActions actions;
};


// If runs == 1, then include width, height, mines, and algorithm.
struct JSONResult {
  std::string url;
  std::optional<CoordType> width;
  std::optional<CoordType> height;
  std::optional<CellType> mines;
  CellType three_bv;
  std::string status;
  // isExact
  std::optional<CellType> minclicks;
  std::optional<int> upper_bound;
  std::optional<int> peak_states;
  std::optional<double> solve_seconds;
  // not isExact
  std::optional<std::string> algorithm;
  std::optional<CellType> clicks;
  // all
  std::optional<CellType> eight_way;
  std::optional<CellType> lzini;
  std::optional<CellType> hzini;
  // witness
  // If all, ExpandedActions.
  // If not all, SingleActions.
  std::optional<FlexibleActions> actions;
};

struct JSONMetadata {
  int runs;
  CoordType width;
  CoordType height;
  CellType mines;
  std::optional<uint64_t> initial_seed;
  // not isExact
  std::optional<std::string> algorithm;
  std::optional<double> elapsed_time;
};

namespace fs = std::filesystem;

struct JSONResultWriter {
  enum class Destination { UNKNOWN, FILE, TERMINAL };
  // Streaming Array Mode: Metadata and Batch writes
  JSONResultWriter(
    std::string file_path,
    JSONMetadata initial_meta
  ) : filename(std::move(file_path) + ".json"), metadata(std::move(initial_meta)), operating_mode(Mode::STREAMING_ARRAY), output_destination(Destination::FILE) {
    init_internals<Mode::STREAMING_ARRAY, Destination::FILE>();
  }
  JSONResultWriter(
    JSONMetadata initial_meta
  ) : metadata(std::move(initial_meta)), operating_mode(Mode::STREAMING_ARRAY), output_destination(Destination::UNKNOWN) {
    init_internals<Mode::STREAMING_ARRAY, Destination::UNKNOWN>();
  }
  // Single Object Mode: Immediately writes output w/o Metadata
  JSONResultWriter(
    std::string file_path,
    const JSONResult &single_output
  ) : filename(std::move(file_path) + ".json"), operating_mode(Mode::SINGLE_OBJECT), output_destination(Destination::FILE) {
    init_internals<Mode::SINGLE_OBJECT, Destination::FILE>();
    output_single(single_output);
  };
  JSONResultWriter(
    const JSONResult &single_output
  ) : operating_mode(Mode::SINGLE_OBJECT), output_destination(Destination::TERMINAL) {
    init_internals<Mode::SINGLE_OBJECT, Destination::TERMINAL>();
    output_single(single_output);
  };
  // Unknown Mode: Mode will be updated upon first action specific to either mode
  JSONResultWriter(std::string file_path) : filename(std::move(file_path) + ".json"), output_destination(Destination::FILE) {
    init_internals<Mode::UNKNOWN, Destination::FILE>();
  }
  JSONResultWriter() : output_destination(Destination::UNKNOWN) {
    init_internals<Mode::UNKNOWN, Destination::UNKNOWN>();
  }
  // Deconstructor: Output remaining data and clean up loose ends
  ~JSONResultWriter();
  // Output Single Result
  void output_single(const JSONResult &single_output);
  // Add result to batch_buffer and output if BATCH_LIMIT` is reached
  void add_result(const JSONResult &result);
  void change_filename(const std::string &new_filename);
  void update_metadata(const JSONMetadata &new_metadata);
  bool set_destination(Destination destination);

private:
  enum class Mode { UNKNOWN, STREAMING_ARRAY, SINGLE_OBJECT };
  std::string filename;
  std::vector<JSONResult> batch_buffer;
  JSONMetadata metadata;
  const size_t BATCH_LIMIT = 100;
  bool is_new_file = true;
  bool has_written = false;
  Mode operating_mode = Mode::UNKNOWN;
  Destination output_destination = Destination::UNKNOWN;
  void write_fresh_header(std::fstream &file);
  void flush_batch();
  template <Mode M, Destination D>
  requires (M == Mode::SINGLE_OBJECT && (D == Destination::TERMINAL || D == Destination::FILE))
  void output_single_result(const JSONResult &single_output);
  /*
  Allowed Combinations
    |      Mode       | Destination |
    | UNKNOWN         | UNKNOWN     |
    | UNKNOWN         | FILE        |
    | SINGLE_OBJECT   | TERMINAL    |
    | SINGLE_OBJECT   | FILE        |
    | STREAMING_ARRAY | UNKNOWN     |
    | STREAMING_ARRAY | FILE        |
  */
  template <Mode M, Destination D>
  requires ((M == Mode::UNKNOWN && (D == Destination::UNKNOWN || D == Destination::FILE)) || (M == Mode::SINGLE_OBJECT && (D == Destination::TERMINAL || D == Destination::FILE)) || (M == Mode::STREAMING_ARRAY && (D == Destination::UNKNOWN || D == Destination::FILE)))
  void init_internals();
};

} // namespace minclicks

#include "json.tpp"
