# The OS utilities of the portable .NET targets: only the errno provider. There
# is no OS layer, so write_to_stderr (io.h) and internal::exit are declared and
# deliberately not defined; a build that references them fails to link.
add_object_library(
  dotnet_util
  SRCS
    errno.cpp
  HDRS
    io.h
  DEPENDS
    libc.src.__support.common
    libc.src.__support.CPP.string_view
    libc.src.__support.libc_errno
    libc.src.__support.macros.config
)
