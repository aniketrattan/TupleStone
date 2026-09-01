// The aggregate library target needs at least one translation unit of its own.
// It stays deliberately empty: everything real lives in a layer target under
// src/<layer>/, and this file exists only so `libnanosql` is a valid archive.
namespace nanosql::internal {

// Referenced by nothing; present so the object file is not empty, which some
// archivers warn about.
const char* LibraryName() { return "nanosql"; }

}  // namespace nanosql::internal
