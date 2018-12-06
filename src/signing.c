#include <Rinternals.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/crypto.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/hmac.h>
#include "utils.h"

SEXP bignum2r(BIGNUM *val);

static const EVP_MD* guess_hashfun(int length){
  switch(length){
  case 16:
    return EVP_md5();
  case 20:
    return EVP_sha1();
  case 24:
    return EVP_sha224();
  case 32:
    return EVP_sha256();
  case 48:
    return EVP_sha384();
  case 64:
    return EVP_sha512();
  }
  return NULL;
}

SEXP R_hash_sign(SEXP md, SEXP key){
  BIO *mem = BIO_new_mem_buf(RAW(key), LENGTH(key));
  EVP_PKEY *pkey = d2i_PrivateKey_bio(mem, NULL);
  BIO_free(mem);
  bail(!!pkey);
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(pkey, NULL);
  bail(!!ctx);
  bail(EVP_PKEY_sign_init(ctx) > 0);
  //bail(EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) >= 0);
  const EVP_MD *md_func = guess_hashfun(LENGTH(md));
  bail(!!md_func);
  bail(EVP_PKEY_CTX_set_signature_md(ctx, md_func) > 0);

  //detemine buffer length (this is really required, over/under estimate can crash)
  size_t siglen;
  bail(EVP_PKEY_sign(ctx, NULL, &siglen, RAW(md), LENGTH(md)) > 0);

  //calculate signature
  unsigned char *sig = OPENSSL_malloc(siglen);
  bail(EVP_PKEY_sign(ctx, sig, &siglen, RAW(md), LENGTH(md)) > 0);
  EVP_PKEY_CTX_free(ctx);
  EVP_PKEY_free(pkey);
  SEXP res = allocVector(RAWSXP, siglen);
  memcpy(RAW(res), sig, siglen);
  OPENSSL_free(sig);
  return res;
}

SEXP R_hash_verify(SEXP md, SEXP sig, SEXP pubkey){
  const unsigned char *ptr = RAW(pubkey);
  EVP_PKEY *pkey = d2i_PUBKEY(NULL, &ptr, LENGTH(pubkey));
  bail(!!pkey);
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(pkey, NULL);
  bail(!!ctx);
  bail(EVP_PKEY_verify_init(ctx) > 0);
  //bail(EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) >= 0);
  const EVP_MD *md_func = guess_hashfun(LENGTH(md));
  bail(!!md_func);
  bail(EVP_PKEY_CTX_set_signature_md(ctx, md_func) > 0);
  int res = EVP_PKEY_verify(ctx, RAW(sig), LENGTH(sig), RAW(md), LENGTH(md));
  bail(res >= 0);
  if(res == 0)
    error("Verification failed: incorrect signature");
  EVP_PKEY_CTX_free(ctx);
  EVP_PKEY_free(pkey);
  return ScalarLogical(1);
}

SEXP R_parse_sig(SEXP buf){
  const char *dsanames[] = {"r", "s", ""};
  const char *rsanames[] = {"s", ""};
  const unsigned char *p = RAW(buf);
  if(Rf_inherits(buf, "ecdsa")){
    ECDSA_SIG *sig = d2i_ECDSA_SIG(NULL, &p, Rf_length(buf));
    bail(!!sig);
    SEXP out = PROTECT(Rf_mkNamed(VECSXP, dsanames));
    SET_VECTOR_ELT(out, 0, bignum2r(sig->r));
    SET_VECTOR_ELT(out, 1, bignum2r(sig->s));
    UNPROTECT(1);
    return out;
  } else if(Rf_inherits(buf, "dsa")){
    DSA_SIG *sig = d2i_DSA_SIG(NULL, &p, Rf_length(buf));
    bail(!!sig);
    SEXP out = PROTECT(Rf_mkNamed(VECSXP, dsanames));
    SET_VECTOR_ELT(out, 0, bignum2r(sig->r));
    SET_VECTOR_ELT(out, 1, bignum2r(sig->s));
    UNPROTECT(1);
    return out;
  } else if(Rf_inherits(buf, "rsa")){
    // I think for RSA the signature itself is just a single bignum?
    SEXP out = PROTECT(Rf_mkNamed(VECSXP, rsanames));
    SEXP val = PROTECT(Rf_duplicate(buf));
    Rf_setAttrib(val, R_ClassSymbol, mkString("bignum"));
    SET_VECTOR_ELT(out, 0, val);
    UNPROTECT(2);
    return out;
  } else {
    Rf_error("Signature must have class 'rsa', 'dsa' or 'ecdsa'");
  }
}
