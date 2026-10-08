#include "json.hpp"

namespace minclicks {

JSONResultWriter::~JSONResultWriter() {
  if (operating_mode == Mode::STREAMING_ARRAY) {
    flush_batch();
  }
  if (output_destination == Destination::FILE && has_written) {
    std::cout << "Wrote JSON output to '" << filename << "'." << std::endl;
  }
}

void JSONResultWriter::output_single(const JSONResult &single_output) {
  if (operating_mode == Mode::STREAMING_ARRAY) {
    return;
  }
  if (operating_mode == Mode::UNKNOWN) {
    operating_mode = Mode::SINGLE_OBJECT;
  }
  if (output_destination == Destination::UNKNOWN) {
    output_destination = Destination::TERMINAL;
  }
  if (output_destination == Destination::FILE) {
    output_single_result<Mode::SINGLE_OBJECT, Destination::FILE>(single_output);
  } else {
    output_single_result<Mode::SINGLE_OBJECT, Destination::TERMINAL>(single_output);
  }
}

void JSONResultWriter::add_result(const JSONResult &result) {
  if (operating_mode == Mode::SINGLE_OBJECT) {
    return;
  }
  if (operating_mode == Mode::UNKNOWN) {
    operating_mode = Mode::STREAMING_ARRAY;
  }

  batch_buffer.push_back(result);
  
  if (batch_buffer.size() >= BATCH_LIMIT) {
    flush_batch();
  }
}

void JSONResultWriter::change_filename(const std::string &new_filename) {
  if (output_destination == Destination::TERMINAL || filename == new_filename) {
    return;
  }
  if (output_destination == Destination::UNKNOWN) {
    output_destination = Destination::FILE;
  }

  if (!has_written) {
    filename = new_filename;
  } else {
    if (operating_mode == Mode::STREAMING_ARRAY && !batch_buffer.empty()) {
      flush_batch();
    }

    try {
      if (fs::exists(filename)) {
        fs::rename(filename, new_filename);
      }
      filename = new_filename;
      // Reset state so that the system treats the newly renamed target as 
      //   unmodified until a brand new streaming block or write method runs.
      is_new_file = false;
    } catch (const fs::filesystem_error &e) {
      std::cerr << "[JSONResultWriter Error] File rename failure: " << e.what() << "\n";
    }
  }
}

void JSONResultWriter::update_metadata(const JSONMetadata &new_metadata) {
  if (operating_mode == Mode::SINGLE_OBJECT || output_destination == Destination::TERMINAL) {
    return;
  }
  if (operating_mode == Mode::UNKNOWN) {
    operating_mode = Mode::STREAMING_ARRAY;
  }
  if (output_destination == Destination::UNKNOWN) {
    output_destination = Destination::UNKNOWN;
  }

  metadata = new_metadata;
  // If the file hasn't been written to disk yet, we can exit early.
  // The new metadata will apply on the first standard flush.
  if (is_new_file || !std::filesystem::exists(filename)) {
    return;
  }
  // Flush active RAM items into the file layout before modifying.
  if (!batch_buffer.empty()) {
    flush_batch();
  }
  // Open the file to isolate the existing results block
  std::fstream file(filename, std::ios::in | std::ios::binary);
  if (!file) {
    return;
  }

  std::string file_contents(
    (std::istreambuf_iterator<char>(file)),
    std::istreambuf_iterator<char>()
  );
  file.close();
  // Pinpoint the entry boundary position of the array contents.
  size_t array_start_pos = file_contents.find("\"run_results\": [");
  if (array_start_pos == std::string::npos) {
    return;
  }
  // Shift index to point right past the trailing newline of the header label block.
  size_t inner_content_pos = file_contents.find('\n', array_start_pos) + 1;
  // Grab only the raw interior array rows payload.
  std::string raw_results_payload = file_contents.substr(inner_content_pos);
  // Re-open and completely rewrite the file with the new header block.
  file.open(filename, std::ios::out | std::ios::binary | std::ios::trunc);
  write_fresh_header(file);
  // Append the original raw array contents back to the file.
  file.write(raw_results_payload.data(), raw_results_payload.size());
  file.close();
}

bool JSONResultWriter::set_destination(JSONResultWriter::Destination destination) {
  if (output_destination != Destination::UNKNOWN) {
    if (output_destination == destination) return true;
    std::runtime_error("JSON destination already set. Cannot be changed.");
    return false;
  }
  if (destination == Destination::UNKNOWN) {
    std::invalid_argument("JSON destination cannot be unknown.");
    return false;
  }
  output_destination = destination;
  return true;
}

void JSONResultWriter::write_fresh_header(std::fstream &file) {
  std::string serialized_meta;
  auto ec_meta = glz::write<glz::opts{.prettify = true}>(metadata, serialized_meta);
  
  if (!ec_meta && serialized_meta.size() > 2) {
    std::string_view meta_head(serialized_meta.data(), serialized_meta.size() - 2);
    file.write(meta_head.data(), meta_head.size());
      
    const std::string array_start = ",\n  \"run_results\": [\n";
    file.write(array_start.data(), array_start.size());
  } else {
    const std::string fallback_start = "{\n  \"run_results\": [\n";
    file.write(fallback_start.data(), fallback_start.size());
  }
}

void JSONResultWriter::flush_batch() {
  if (output_destination == Destination::UNKNOWN) {
    output_destination = Destination::FILE;
  }
  // Skip entirely if in single-object mode or nothing to write
  if (operating_mode == Mode::SINGLE_OBJECT || batch_buffer.empty()) {
    return;
  }

  std::fstream file;

  if (is_new_file) {
    file.open(filename, std::ios::out | std::ios::binary | std::ios::trunc);
    write_fresh_header(file);
    is_new_file = false; 
  } else {
    file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    file.seekp(-6, std::ios::end);
    const std::string comma_split = ",\n";
    file.write(comma_split.data(), comma_split.size());
  }

  std::string serialized_chunk;
  auto ec = glz::write_json(batch_buffer, serialized_chunk);
  
  if (!ec) {
    const std::string row_indent = "    ";
    file.write(row_indent.data(), row_indent.size());
    std::string_view inner_json(serialized_chunk.data() + 1, serialized_chunk.size() - 2);
    file.write(inner_json.data(), inner_json.size());
    has_written = true;
  }

  const std::string array_end = "\n  ]\n}";
  file.write(array_end.data(), array_end.size());
  file.close();

  batch_buffer.clear();
}

} // namespace minclicks



