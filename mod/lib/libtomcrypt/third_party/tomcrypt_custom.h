/* LibTomCrypt, modular cryptographic library -- Tom St Denis */
/* SPDX-License-Identifier: Unlicense */

#ifndef TOMCRYPT_CUSTOM_H_
#define TOMCRYPT_CUSTOM_H_

/* macros for various libc functions you can change for embedded targets */
#ifndef XMEMCPY
#define XMEMCPY  memcpy
#endif

/* ---> One-Way Hash Functions <--- */
#define LTC_SHA256

#endif /* TOMCRYPT_CUSTOM_H_ */
