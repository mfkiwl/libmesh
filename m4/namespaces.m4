dnl -------------------------------------------------------------
dnl AC_CXX_HAVE_NAMESPACES
dnl -------------------------------------------------------------
AC_DEFUN([AC_CXX_NAMESPACES],
[AC_CACHE_CHECK(whether the compiler implements namespaces,
ac_cv_cxx_namespaces,
[AC_LANG_SAVE
 AC_LANG([C++])
 AC_COMPILE_IFELSE([AC_LANG_PROGRAM([namespace Outer { namespace Inner { int i = 0; }}],
                                   [using namespace Outer::Inner; return i;])],
 ac_cv_cxx_namespaces=yes, ac_cv_cxx_namespaces=no)
 AC_LANG_RESTORE
])
AS_IF([test "x$ac_cv_cxx_namespaces" = "xyes"],
      [AC_DEFINE(HAVE_NAMESPACES,,[define if the compiler implements namespaces])])
])
