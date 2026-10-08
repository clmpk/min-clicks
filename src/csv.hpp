#pragma once

#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <optional>
#include <variant>
#include <array>
#include <string_view>
#include "board.hpp"

namespace minclicks {

struct CSVResult {
  std::string url;
  std::optional<CoordType> width;
  std::optional<CoordType> height;
  std::optional<CellType> mines;
  CellType three_bv;
  std::string status;
  std::optional<CellType> minclicks;
  std::optional<int> upper_bound;
  std::optional<int> peak_states;
  std::optional<double> solve_seconds;
  std::optional<std::string> algorithm;
  std::optional<CellType> clicks;
  std::optional<CellType> eight_way;
  std::optional<CellType> lzini;
  std::optional<CellType> hzini;
};

enum class CSVField {
  url,
  height,
  width,
  mines,
  three_bv,
  status,
  minclicks,
  upper_bound,
  peak_states,
  solve_seconds,
  algorithm,
  clicks,
  eight_way,
  lzini,
  hzini,
  COUNT
};

struct FieldMapping {
  CSVField field;
  std::string_view name;
};

inline constexpr std::array<FieldMapping, static_cast<size_t>(CSVField::COUNT)> CSV_FIELD_NAMES = {{
  { CSVField::url,           "url" },
  { CSVField::height,        "h" },
  { CSVField::width,         "w" },
  { CSVField::mines,         "m" },
  { CSVField::three_bv,      "3bv" },
  { CSVField::status,        "Status"},
  { CSVField::minclicks,     "Min Clicks" },
  { CSVField::upper_bound,   "Upper Bound" },
  { CSVField::peak_states,   "Peak States" },
  { CSVField::solve_seconds, "Solve Sec"},
  { CSVField::algorithm,     "Alg" },
  { CSVField::clicks,        "Clicks" },
  { CSVField::eight_way,     "8way" },
  { CSVField::lzini,         "lzini" },
  { CSVField::hzini,         "hzini" }
}};

[[nodiscard]] inline constexpr std::string_view get_field_name(CSVField field) noexcept {
  return CSV_FIELD_NAMES[static_cast<size_t>(field)].name;
}

namespace fs = std::filesystem;

struct CSVResultWriter {

  explicit CSVResultWriter(const std::string& filename);
  explicit CSVResultWriter();

  ~CSVResultWriter();

  void add_result(const CSVResult &result);
  void add_results(const std::vector<CSVResult>& results);
  void change_filename(const std::string &new_filename);

private:
  enum class Destination { UNKNOWN, TERMINAL, FILE };
  std::string filename;
  std::vector<CSVResult> batch_buffer;
  const size_t BATCH_LIMIT = 100;
  bool is_new_file = true;
  bool has_written = false;
  Destination output_destination = Destination::FILE;
  std::vector<char> io_buffer;

  std::string get_csv_headers() const;
  void write_fresh_header(std::ostream &os);
  void flush_batch();
};

} // namespace minclicks