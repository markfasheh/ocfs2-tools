# MB_VENDOR([VARIABLE])
# ---------------------
AC_DEFUN([MB_VENDOR],
  [AC_MSG_CHECKING([for vendor])
   AC_ARG_WITH(vendor, [  --with-vendor=VENDOR    Vendor to tailor build defaults and packages to [common]],[
     mb_vendor="$withval"
     if test -x "vendor/${mb_vendor}/vendor.guess"; then
       if "vendor/${mb_vendor}/vendor.guess" >&AS_MESSAGE_LOG_FD 2>&AS_MESSAGE_LOG_FD; then
         AC_MSG_RESULT([$mb_vendor])
       else
         AC_MSG_RESULT([not found])
         AC_MSG_ERROR([Vendor $mb_vendor not detected])
       fi
     else
       AC_MSG_RESULT([not supported])
       AC_MSG_ERROR([Vendor $mb_vendor not supported])
     fi
  ], [
    mb_vendor=`./vendor.guess 2>&AS_MESSAGE_LOG_FD`
    if test -z "$mb_vendor"; then
       AC_MSG_RESULT([not found])
    else
       AC_MSG_RESULT([$mb_vendor])
    fi
  ])
  dnl Use 2.13 safe ifelse()
  ifelse([$1], [], [], [
           $1="$mb_vendor"
           AC_SUBST($1)
         ])
])  # MB_VENDOR

# MB_VENDOR_KERNEL([VARIABLE])
# ---------------------
AC_DEFUN([MB_VENDOR_KERNEL],
  [AC_MSG_CHECKING([for vendor kernel])
   AC_ARG_WITH(vendorkernel, [  --with-vendorkernel=KERNELVERSION  Vendor kernel version to compile against [detected]], [
     mb_vendorkernel="$withval"
     if test -z "$mb_vendor"; then
       AC_MSG_RESULT([no vendor])
       AC_MSG_ERROR([No vendor specified or discovered])
     fi
     if test -x "vendor/${mb_vendor}/kernel.guess"; then
       mb_vkinclude="`vendor/${mb_vendor}/kernel.guess build ${mb_vendorkernel} 2>&AS_MESSAGE_LOG_FD`"
       if test -z "$mb_vkinclude"; then
         AC_MSG_RESULT([not found])
         AC_MSG_ERROR([Vendor kernel $mb_vendorkernel not detected])
       else
         AC_MSG_RESULT([$mb_vkinclude])
       fi
     else
       AC_MSG_RESULT([not supported])
       AC_MSG_ERROR([Vendor $mb_vendor does not support kernel detection])
     fi
   ], [
     if test -x "vendor/${mb_vendor}/kernel.guess"; then
       mb_vkinclude="`vendor/${mb_vendor}/kernel.guess build 2>&AS_MESSAGE_LOG_FD`"
       if test -z "$mb_vkinclude"; then
         AC_MSG_RESULT([not found])
       else
         AC_MSG_RESULT([$mb_vkinclude])
       fi
     else
       mb_vkinclude=
       AC_MSG_RESULT([not supported])
     fi
  ])
  dnl Use 2.13 safe ifelse()
  ifelse([$1], [], [], [
           $1="$mb_vkinclude"
           AC_SUBST($1)
         ])
])  # MB_VENDOR_KERNEL
