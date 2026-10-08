#ifndef JSON_TPP
#define JSON_TPP

namespace minclicks {

// JSONResultWriter Template Functions
template<JSONResultWriter::Mode M>
requires (M == JSONResultWriter::Mode::UNKNOWN)
void init_internals() {}
template<JSONResultWriter::Mode M>
requires (M == JSONResultWriter::Mode::SINGLE_OBJECT)
void init_internals() {}
template<JSONResultWriter::Mode M>
requires (M == JSONResultWriter::Mode::STREAMING_ARRAY)
void init_internals() {}

template <JSONResultWriter::Mode M, JSONResultWriter::Destination D>
requires (
  M == JSONResultWriter::Mode::SINGLE_OBJECT 
  && (D == JSONResultWriter::Destination::TERMINAL || D == JSONResultWriter::Destination::FILE)
)
void JSONResultWriter::output_single_result(const JSONResult &single_output) {
  if constexpr (D == JSONResultWriter::Destination::FILE) {
    std::ofstream file(filename, std::ios::out | std::ios::binary | std::ios::trunc);
    if (file) {
      std::string buffer;
      auto ec = glz::write<glz::opts{.prettify = true}>(single_output, buffer);
      if (!ec) {
        file.write(buffer.data(), buffer.size());
        has_written = true;
      }
    }
  } else {
    std::runtime_error("Single JSON Result terminal output is not implemented yet.");
  }
}
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
template <JSONResultWriter::Mode M, JSONResultWriter::Destination D>
requires (
  (
    M == JSONResultWriter::Mode::UNKNOWN 
    && (D == JSONResultWriter::Destination::UNKNOWN || D == JSONResultWriter::Destination::FILE)
  ) || (
    M == JSONResultWriter::Mode::SINGLE_OBJECT
    && (D == JSONResultWriter::Destination::TERMINAL || D == JSONResultWriter::Destination::FILE)
  ) || (
    M == JSONResultWriter::Mode::STREAMING_ARRAY
    && (D == JSONResultWriter::Destination::UNKNOWN || D == JSONResultWriter::Destination::FILE)
  )
)
void JSONResultWriter::init_internals() {
  // Nothing to setup internally yet.
  return;
  if constexpr (M == JSONResultWriter::Mode::SINGLE_OBJECT) {
    // Nothing
  } else if constexpr (M == JSONResultWriter::Mode::STREAMING_ARRAY) {
    // Nothing
  } else {
    // Nothing
  }
  if constexpr (D == JSONResultWriter::Destination::TERMINAL) {
    // Nothing
  } else if constexpr (D == JSONResultWriter::Destination::FILE) {
    // Nothing
  } else {
    // Nothing
  }
}

} // namespace minclicks

// Register AllActions as a standard JSON Object
template <>
struct glz::meta<minclicks::ExpandedAction> {
  using T = minclicks::ExpandedAction;
  static constexpr auto value = array(&T::kind, &T::row, &T::column);
};

template <>
struct glz::meta<minclicks::AllActions> {
  using T = minclicks::AllActions;
  static constexpr auto value = object(
    "optimal", &T::optimal,
    "way8", &T::way8,
    "lzini", &T::lzini,
    "hzini", &T::hzini
  );
};

template <>
struct glz::meta<minclicks::RootDocument> {
  using T = minclicks::RootDocument;
  static constexpr auto value = object(
    "profile_name", &T::profile_name,
    "actions", &T::actions
  );
};

#endif