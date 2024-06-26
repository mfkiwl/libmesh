# ------------------------------------------------------------------------------
# Choose the best thread option available, if any
# ------------------------------------------------------------------------------
AC_DEFUN([ACX_BEST_THREAD],
[
  AC_ARG_WITH(thread-model,
              AS_HELP_STRING([--with-thread-model=tbb,pthread,openmp,auto,none],[Specify the thread model to use]),
              [AS_CASE("${withval}",
                       [tbb],      [requested_thread_model=tbb],
                       [pthread],  [requested_thread_model=pthread],
                       [pthreads], [requested_thread_model=pthread],
                       [openmp],   [requested_thread_model=openmp],
                       [auto],     [requested_thread_model=auto],
                       [none],     [requested_thread_model=none],
                       [AC_MSG_ERROR(bad value ${withval} for --with-thread-model)])],
              requested_thread_model=auto)

  AC_MSG_RESULT([<<< User requested thread model: $requested_thread_model >>>])

  dnl We *always* detect the compiler's OpenMP support unless explicitly told not
  dnl to with --disable-openmp, because libMesh is frequently compiled with other
  dnl libraries that use OpenMP directives (it can also use a few itself [0]) and
  dnl if you don't set OMP_NUM_THREADS in the environment or call
  dnl omp_set_num_threads() programmatically, you get the questionable default of
  dnl "1 thread per CPU" [1]. This can lead to drastic over-subscription of
  dnl hardware. For example, an MPI code run with "mpiexec -np 4" on a
  dnl machine with 4 physical cores will try and spawn up to 16
  dnl simultaneous threads in this configuration.
  dnl
  dnl [0]: The "pthread" threading model (see below) can optionally use
  dnl OpenMP pragmas for its parallel_for() and parallel_reduce()
  dnl implementations.
  dnl
  dnl [1]: https://gcc.gnu.org/onlinedocs/libgomp/OMP_005fNUM_005fTHREADS.html
  AC_ARG_ENABLE(openmp,
                AS_HELP_STRING([--disable-openmp],
                               [build without OpenMP support]),
                [AS_CASE("${enableval}",
                         [yes], [enableopenmp=yes],
                         [no],  [enableopenmp=no],
                         [AC_MSG_ERROR(bad value ${enableval} for --enable-openmp)])],
                [enableopenmp=yes])

  AS_IF([test "x$enableopenmp" = "xyes"],
        [
          AC_LANG_PUSH([C++])
          AX_OPENMP([],[enableopenmp=no])
          AC_LANG_POP()
          AC_LANG_PUSH([C])
          AX_OPENMP([],[])
          AC_LANG_POP()
          AS_IF([test "x$enablefortran" = "xyes"],
                [
                  AC_LANG_PUSH([Fortran 77])
                  AX_OPENMP([],[])
                  AC_LANG_POP()
                ])

          AS_IF([test "x$OPENMP_CFLAGS" != "x"],
                [
                  AC_MSG_RESULT(<<< Configuring library with C OpenMP support >>>)
                  CFLAGS_OPT="$CFLAGS_OPT $OPENMP_CFLAGS"
                  CFLAGS_DBG="$CFLAGS_DBG $OPENMP_CFLAGS"
                  CFLAGS_DEVEL="$CFLAGS_DEVEL $OPENMP_CFLAGS"
                  CFLAGS_PROF="$CFLAGS_PROF $OPENMP_CFLAGS"
                  CFLAGS_OPROF="$CFLAGS_OPROF $OPENMP_CFLAGS"
                  AC_SUBST(OPENMP_CFLAGS)
                ])

          AS_IF([test "x$OPENMP_FFLAGS" != "x"],
                [
                  AC_MSG_RESULT(<<< Configuring library with Fortran OpenMP support >>>)
                  FFLAGS="$FFLAGS $OPENMP_FFLAGS"
                  AC_SUBST(OPENMP_FFLAGS)
                ])

          AS_IF([test "x$OPENMP_CXXFLAGS" != "x"],
                [
                  AC_MSG_RESULT(<<< Configuring library with OpenMP support >>>)
                  CXXFLAGS_OPT="$CXXFLAGS_OPT $OPENMP_CXXFLAGS"
                  CXXFLAGS_DBG="$CXXFLAGS_DBG $OPENMP_CXXFLAGS"
                  CXXFLAGS_DEVEL="$CXXFLAGS_DEVEL $OPENMP_CXXFLAGS"
                  CXXFLAGS_PROF="$CXXFLAGS_PROF $OPENMP_CXXFLAGS"
                  CXXFLAGS_OPROF="$CXXFLAGS_OPROF $OPENMP_CXXFLAGS"
                  AC_SUBST(OPENMP_CXXFLAGS)
                ],
                dnl If the user explicitly requested the openmp threading model and
                dnl this compiler doesn't support OpenMP for some reason, we throw an
                dnl error instead of silently continuing.
                [AS_IF([test "x$requested_thread_model" = "xopenmp"],
                       [AC_MSG_ERROR([requested openmp threading model, but C++ compiler does not support openmp.])])])],
        dnl Error if the user simultaneously requested --with-thread-model=openmp and --disable-openmp
        [AS_IF([test "x$requested_thread_model" = "xopenmp"],
               [AC_MSG_ERROR([requested openmp threading model, but --disable-openmp has been specified.])])])

  dnl Set this variable when a threading model is found
  found_thread_model=none

  dnl First, try pthreads/openmp as long as the user requested it (or auto).
  AS_IF([test "x$requested_thread_model" = "xpthread" || test "x$requested_thread_model" = "xauto" || test "x$requested_thread_model" = "xopenmp"],
        [
          dnl Let the user explicitly specify --{enable,disable}-pthreads.
          AC_ARG_ENABLE(pthreads,
                        AS_HELP_STRING([--disable-pthreads],
                                       [build without POSIX threading (pthreads) support]),
                        [AS_CASE("${enableval}",
                                 [yes], [enablepthreads=yes],
                                 [no],  [enablepthreads=no],
                                 [AC_MSG_ERROR(bad value ${enableval} for --enable-pthreads)])],
                        [enablepthreads=$enableoptional])

          AS_IF([test "x$enablepthreads" = "xyes"],
                [
                  AX_PTHREAD
                  AS_IF([test "x$ax_pthread_ok" = "xyes"],
                        [
                          AC_DEFINE(USING_THREADS, 1, [Flag indicating whether the library shall be compiled to use any particular thread API.])
                          AC_MSG_RESULT(<<< Configuring library with pthread support >>>)
                          libmesh_optional_INCLUDES="$PTHREAD_CFLAGS $libmesh_optional_INCLUDES"
                          libmesh_optional_LIBS="$PTHREAD_LIBS $libmesh_optional_LIBS"
                          found_thread_model=pthread
                        ],
                        [enablepthreads=no])
                ])

          dnl If pthreads were not enabled, but the user requested it, we treat that as an error:
          dnl we want to alert the user as soon as possible that their requested thread model
          dnl could not be configured correctly.
          AS_IF([test "x$enablepthreads" = "xno" && test "x$requested_thread_model" = "xpthread"],
                [AC_MSG_ERROR([requested threading model, pthreads, could not be found.])])
          AS_IF([test "x$enablepthreads" = "xno" && test "x$requested_thread_model" = "xopenmp"],
                [AC_MSG_ERROR([openmp threading model requested, but required pthread support unavailable.])])
        ])

  dnl Try to configure TBB if the user explicitly requested it, or if we
  dnl are doing auto detection and pthreads/openmp detection did not
  dnl succeed.
  AS_IF([test "x$found_thread_model" = "xnone"],
        [
          AS_IF([test "x$requested_thread_model" = "xtbb" || test "x$requested_thread_model" = "xauto"],
                [
                  CONFIGURE_TBB
                  AS_IF([test "x$enabletbb" = "xyes"],
                        [
                          libmesh_optional_INCLUDES="$TBB_INCLUDE $libmesh_optional_INCLUDES"
                          libmesh_optional_LIBS="$TBB_LIBRARY $libmesh_optional_LIBS"
                          found_thread_model=tbb
                        ])

                  dnl If TBB was not enabled, but the user requested it, we treat that as an error:
                  dnl we want to alert the user as soon as possible that their requested thread model
                  dnl could not be configured correctly.
                  AS_IF([test "x$enabletbb" = "xno" && test "x$requested_thread_model" = "xtbb"],
                        [AC_MSG_ERROR([requested threading model, TBB, could not be found.])])
                ])
        ])

  AC_MSG_RESULT([<<< Found thread model: $found_thread_model >>>])
])
