dnl Support for libblkid included in our tree for ocfs2console

AC_DEFUN([OCFS2_BLKID], [
  HAVE_BLKID=
  PKG_CHECK_MODULES(BLKID, blkid >= 1.36, HAVE_BLKID=yes,
    [AC_MSG_WARN([blkid >= 1.36 not found, using internal version])])
  AC_SUBST(HAVE_BLKID)

  if test "x$HAVE_BLKID" != "xyes"; then
    AC_CHECK_LIB(uuid, uuid_unparse, :,
      [AC_MSG_ERROR([could not find uuid library])])

    AC_CHECK_SIZEOF(short)
    AC_CHECK_SIZEOF(int)
    AC_CHECK_SIZEOF(long)
    AC_CHECK_SIZEOF(long long)

    AC_CONFIG_COMMANDS([ocfs2console/blkid/blkid_types.h], [
      outfile=ocfs2console/blkid/blkid_types.h-tmp
      cat > $outfile <<_______EOF
/* 
 * If linux/types.h is already been included, assume it has defined
 * everything we need.  (cross fingers)  Other header files may have 
 * also defined the types that we need.
 */
#if (!defined(_LINUX_TYPES_H) && !defined(_BLKID_TYPES_H) && \\
	!defined(_EXT2_TYPES_H))
#define _BLKID_TYPES_H

typedef unsigned char __u8;
typedef signed char __s8;

#if ($ocfs2_SIZEOF_INT == 8)
typedef int		__s64;
typedef unsigned int	__u64;
#else
#if ($ocfs2_SIZEOF_LONG == 8)
typedef long		__s64;
typedef unsigned long	__u64;
#else
#if ($ocfs2_SIZEOF_LONG_LONG == 8)
#if defined(__GNUC__)
typedef __signed__ long long 	__s64;
#else
typedef signed long long 	__s64;
#endif /* __GNUC__ */
typedef unsigned long long	__u64;
#endif /* SIZEOF_LONG_LONG == 8 */
#endif /* SIZEOF_LONG == 8 */
#endif /* SIZEOF_INT == 8 */

#if ($ocfs2_SIZEOF_INT == 2)
typedef	int		__s16;
typedef	unsigned int	__u16;
#else
#if ($ocfs2_SIZEOF_SHORT == 2)
typedef	short		__s16;
typedef	unsigned short	__u16;
#else
  ?==error: undefined 16 bit type
#endif /* SIZEOF_SHORT == 2 */
#endif /* SIZEOF_INT == 2 */

#if ($ocfs2_SIZEOF_INT == 4)
typedef	int		__s32;
typedef	unsigned int	__u32;
#else
#if ($ocfs2_SIZEOF_LONG == 4)
typedef	long		__s32;
typedef	unsigned long	__u32;
#else
#if ($ocfs2_SIZEOF_SHORT == 4)
typedef	short		__s32;
typedef	unsigned short	__u32;
#else
 ?== error: undefined 32 bit type
#endif /* SIZEOF_SHORT == 4 */
#endif /* SIZEOF_LONG == 4 */
#endif /* SIZEOF_INT == 4 */

#endif /* _*_TYPES_H */
_______EOF

      if cmp -s $outfile ocfs2console/blkid/blkid_types.h; then
        AC_MSG_NOTICE([ocfs2console/blkid/blkid_types.h is unchanged])
        rm -f $outfile
      else
        mv $outfile ocfs2console/blkid/blkid_types.h
      fi
    ],[
      ocfs2_SIZEOF_SHORT=$ac_cv_sizeof_short
      ocfs2_SIZEOF_INT=$ac_cv_sizeof_int
      ocfs2_SIZEOF_LONG=$ac_cv_sizeof_long
      ocfs2_SIZEOF_LONG_LONG=$ac_cv_sizeof_long_long
    ])
  fi
])
