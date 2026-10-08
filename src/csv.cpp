#include "csv.hpp"
#include <charconv>
#include <system_error>

namespace minclicks {

// Extremely fast helper to write primitives using std::to_chars
template <typename T>
inline void write_val(std::ostream& file, const T& val) {
  char buf[64];
  auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
  if (ec == std::errc{}) {
    file.write(buf, ptr - buf);
  }
}

// Overload for handling std::optional fields
template <typename T>
inline void write_opt(std::ostream& file, const std::optional<T>& opt) {
  if (opt.has_value()) {
    write_val(file, *opt);
  }
}

// Overload for optional strings
inline void write_opt_str(std::ostream& file, const std::optional<std::string>& opt) {
  if (opt.has_value()) {
    file.write(opt->data(), opt->size());
  }
}

inline void write_row(std::ostream& file, const CSVResult& row) {
  file.write(row.url.data(), row.url.size()); file.put(',');
  write_opt(file, row.height); file.put(',');
  write_opt(file, row.width); file.put(',');
  write_opt(file, row.mines); file.put(',');
  write_val(file, row.three_bv); file.put(',');
  file.write(row.status.data(), row.status.size()); file.put(',');
  write_opt(file, row.minclicks); file.put(',');
  write_opt(file, row.upper_bound); file.put(',');
  write_opt(file, row.peak_states); file.put(',');
  write_opt(file, row.solve_seconds); file.put(',');
  write_opt_str(file, row.algorithm); file.put(',');
  write_opt(file, row.clicks); file.put(',');
  write_opt(file, row.eight_way); file.put(',');
  write_opt(file, row.lzini); file.put(',');
  write_opt(file, row.hzini);
  file.put('\n');
}

CSVResultWriter::CSVResultWriter(const std::string& file_path) 
  : filename(std::move(file_path) + ".csv"), output_destination(Destination::FILE) {}

CSVResultWriter::CSVResultWriter() 
  : output_destination(Destination::UNKNOWN) {}

CSVResultWriter::~CSVResultWriter() {
  if (!batch_buffer.empty()) {
    flush_batch();
  }
  if (output_destination == Destination::FILE && has_written) {
    std::cout << "Wrote CSV output to '" << filename << "'." << std::endl;
  }
}

void CSVResultWriter::add_result(const CSVResult &result) {
  if (output_destination == Destination::UNKNOWN) {
    output_destination = Destination::TERMINAL;
  }
  batch_buffer.push_back(result);
  if (batch_buffer.size() >= BATCH_LIMIT) {
    flush_batch();
  }
}

void CSVResultWriter::add_results(const std::vector<CSVResult> &results) {
  if (output_destination == Destination::UNKNOWN) {
    output_destination = Destination::TERMINAL;
  }
  for (const auto& row : results) {
    batch_buffer.push_back(row);
    if (batch_buffer.size() >= BATCH_LIMIT) {
      flush_batch();
    }
  }
}

void CSVResultWriter::change_filename(const std::string &new_filename) {
  if (output_destination == Destination::TERMINAL || filename == new_filename) {
    return;
  }
  if (output_destination == Destination::UNKNOWN) {
    output_destination = Destination::FILE;
  }

  std::string target_filename = new_filename;
  if (target_filename.rfind(".csv") == std::string::npos) {
  target_filename += ".csv";
  }

  if (!has_written) {
    filename = target_filename;
  } else {
    if (!batch_buffer.empty()) {
      flush_batch();
    }

    try {
      if (fs::exists(filename)) {
      fs::rename(filename, target_filename);
      }
      filename = target_filename;
      is_new_file = false;
    } catch (const fs::filesystem_error &e) {
      std::cerr << "[CSVResultWriter Error] File rename failure: " << e.what() << "\n";
    }
  }
}

std::string CSVResultWriter::get_csv_headers() const {
  std::string headers;
  for (size_t i = 0; i < static_cast<size_t>(CSVField::COUNT); ++i) {
    headers.append(get_field_name(static_cast<CSVField>(i)));
    if (i + 1 < static_cast<size_t>(CSVField::COUNT)) {
      headers.push_back(',');
    }
  }
  headers.push_back('\n');
  return headers;
}

void CSVResultWriter::write_fresh_header(std::ostream &os) {
  std::string headers = get_csv_headers();
  os.write(headers.data(), headers.size());
}

void CSVResultWriter::flush_batch() {
  if (batch_buffer.empty()) {
    return;
  }

  if (output_destination == Destination::FILE) {
    std::ofstream file(filename, std::ios::out | std::ios::binary | (is_new_file ? std::ios::trunc : std::ios::app));
    if (!file) {
      return;
    }

    // Expand the stream buffer to 64KB to bundle disk updates
    std::vector<char> buffer(16 * 1024);
    file.rdbuf()->pubsetbuf(buffer.data(), buffer.size());

    if (is_new_file) {
      write_fresh_header(file);
      is_new_file = false;
    }

    for (const auto &row : batch_buffer) {
      write_row(file, row);
    }
    has_written = true;
    file.close();
  } else {
    if (is_new_file) {
      std::string headers = get_csv_headers();
      std::cout.write(headers.data(), headers.size());
      is_new_file = false;
    }
    for (const auto &row : batch_buffer) {
      write_row(std::cout, row);
    }
    has_written = true;
  }

  batch_buffer.clear();
}

} // namespace minclicks