/* LibTomCrypt, modular cryptographic library -- Tom St Denis */
/* SPDX-License-Identifier: Unlicense */

/* ---- HASH FUNCTIONS ---- */
#ifdef LTC_SHA256
struct sha256_state {
    ulong64 length;
    ulong32 state[8], curlen;
    unsigned char buf[64];
};
#endif

typedef union Hash_state {
    char dummy[1];
#ifdef LTC_SHA256
    struct sha256_state sha256;
#endif

    void *data;
} hash_state;

/** hash descriptor */
extern  struct ltc_hash_descriptor {
    /** name of hash */
    const char *name;
    /** internal ID */
    unsigned char ID;
    /** Size of digest in octets */
    unsigned long hashsize;
    /** Input block size in octets */
    unsigned long blocksize;
    /** ASN.1 OID */
    unsigned long OID[16];
    /** Length of DER encoding */
    unsigned long OIDlen;

    /** Init a hash state
      @param hash   The hash to initialize
      @return CRYPT_OK if successful
    */
    int (*init)(hash_state *hash);
    /** Process a block of data
      @param hash   The hash state
      @param in     The data to hash
      @param inlen  The length of the data (octets)
      @return CRYPT_OK if successful
    */
    int (*process)(hash_state *hash, const unsigned char *in, unsigned long inlen);
    /** Produce the digest and store it
      @param hash   The hash state
      @param out    [out] The destination of the digest
      @return CRYPT_OK if successful
    */
    int (*done)(hash_state *hash, unsigned char *out);
    /** Self-test
      @return CRYPT_OK if successful, CRYPT_NOP if self-tests have been disabled
    */
    int (*test)(void);

    /* accelerated hmac callback: if you need to-do multiple packets just use the generic hmac_memory and provide a hash callback */
    int  (*hmac_block)(const unsigned char *key, unsigned long  keylen,
                       const unsigned char *in,  unsigned long  inlen,
                             unsigned char *out, unsigned long *outlen);

} hash_descriptor[];

#ifdef LTC_SHA256
int sha256_init(hash_state * md);
int sha256_process(hash_state * md, const unsigned char *in, unsigned long inlen);
int sha256_done(hash_state * md, unsigned char *out);
int sha256_test(void);
extern const struct ltc_hash_descriptor sha256_desc;

#define sha256_c_init sha256_init
int sha256_c_process(hash_state * md, const unsigned char *in, unsigned long inlen);
int sha256_c_done(hash_state * md, unsigned char *out);
int sha256_c_test(void);
extern const struct ltc_hash_descriptor sha256_portable_desc;

#ifdef LTC_SHA256_X86
#define sha256_x86_init sha256_init
int sha256_x86_process(hash_state * md, const unsigned char *in, unsigned long inlen);
int sha256_x86_done(hash_state * md, unsigned char *out);
int sha256_x86_test(void);
extern const struct ltc_hash_descriptor sha256_x86_desc;
#endif /* LTC_SHA256_X86 */
#endif /* LTC_SHA256 */
