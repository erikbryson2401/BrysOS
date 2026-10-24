/* BrysOS: intercambio de bytes y orden de bytes (x86 es little-endian) */
#ifndef _BRYS_LINUX_SWAB_H
#define _BRYS_LINUX_SWAB_H

#include <linux/types.h>

#define swab16(x) ((u16)__builtin_bswap16((u16)(x)))
#define swab32(x) ((u32)__builtin_bswap32((u32)(x)))
#define swab64(x) ((u64)__builtin_bswap64((u64)(x)))
#define __swab16 swab16
#define __swab32 swab32
#define __swab64 swab64

#define cpu_to_le16(x) ((__le16)(u16)(x))
#define cpu_to_le32(x) ((__le32)(u32)(x))
#define cpu_to_le64(x) ((__le64)(u64)(x))
#define le16_to_cpu(x) ((u16)(__le16)(x))
#define le32_to_cpu(x) ((u32)(__le32)(x))
#define le64_to_cpu(x) ((u64)(__le64)(x))
#define cpu_to_be16(x) ((__be16)swab16(x))
#define cpu_to_be32(x) ((__be32)swab32(x))
#define cpu_to_be64(x) ((__be64)swab64(x))
#define be16_to_cpu(x) swab16((u16)(__be16)(x))
#define be32_to_cpu(x) swab32((u32)(__be32)(x))
#define be64_to_cpu(x) swab64((u64)(__be64)(x))
#define __cpu_to_le16 cpu_to_le16
#define __cpu_to_le32 cpu_to_le32
#define __le16_to_cpu le16_to_cpu
#define __le32_to_cpu le32_to_cpu
#define __cpu_to_be16 cpu_to_be16
#define __cpu_to_be32 cpu_to_be32
#define __be16_to_cpu be16_to_cpu
#define __be32_to_cpu be32_to_cpu
#define htons(x) cpu_to_be16(x)
#define ntohs(x) be16_to_cpu(x)
#define htonl(x) cpu_to_be32(x)
#define ntohl(x) be32_to_cpu(x)
#define le16_to_cpup(p) le16_to_cpu(*(const __le16*)(p))
#define le32_to_cpup(p) le32_to_cpu(*(const __le32*)(p))
#define be16_to_cpup(p) be16_to_cpu(*(const __be16*)(p))
#define be32_to_cpup(p) be32_to_cpu(*(const __be32*)(p))
#define le16_add_cpu(p, v) (*(p) = cpu_to_le16(le16_to_cpu(*(p)) + (v)))
#define le32_add_cpu(p, v) (*(p) = cpu_to_le32(le32_to_cpu(*(p)) + (v)))
#define cpu_to_le16s(p) do { } while (0)
#define le16_to_cpus(p) do { } while (0)
#define cpu_to_le32s(p) do { } while (0)
#define le32_to_cpus(p) do { } while (0)
#define __LITTLE_ENDIAN 1234
#define __LITTLE_ENDIAN_BITFIELD

/* Accesos sin alinear (x86 los admite) */
#define get_unaligned(p) ({ __typeof__(*(p)) __v; __builtin_memcpy(&__v, (p), sizeof(__v)); __v; })
#define put_unaligned(v, p) do { __typeof__(*(p)) __v = (v); __builtin_memcpy((p), &__v, sizeof(__v)); } while (0)
#define get_unaligned_le16(p) le16_to_cpu(get_unaligned((const u16*)(p)))
#define get_unaligned_le32(p) le32_to_cpu(get_unaligned((const u32*)(p)))
#define get_unaligned_le64(p) le64_to_cpu(get_unaligned((const u64*)(p)))
#define get_unaligned_be16(p) be16_to_cpu(get_unaligned((const u16*)(p)))
#define get_unaligned_be32(p) be32_to_cpu(get_unaligned((const u32*)(p)))
#define put_unaligned_le16(v, p) put_unaligned(cpu_to_le16(v), (u16*)(p))
#define put_unaligned_le32(v, p) put_unaligned(cpu_to_le32(v), (u32*)(p))
#define put_unaligned_le64(v, p) put_unaligned(cpu_to_le64(v), (u64*)(p))
#define put_unaligned_be16(v, p) put_unaligned(cpu_to_be16(v), (u16*)(p))
#define put_unaligned_be32(v, p) put_unaligned(cpu_to_be32(v), (u32*)(p))

#endif
