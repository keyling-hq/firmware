/*
 * Ed25519 and X25519 for canokey-crypto. Upstream gets these from a patched TF-PSA-Crypto;
 * ESP-IDF's copy isn't patched, so the weak functions in ecc.c are replaced with Monocypher.
 */
#include <string.h>
#include <ecc.h>
#include <memzero.h>
#include <ml-dsa-65.h>
#include <rand.h>
#include <sha.h>
#include "monocypher.h"
#include "monocypher-ed25519.h"

static void expand_secret(const K__ed25519_secret_key sk, uint8_t scalar[32], uint8_t prefix[32])
{
    uint8_t digest[SHA512_DIGEST_LENGTH];
    sha512_raw(sk, sizeof(K__ed25519_secret_key), digest);
    crypto_eddsa_trim_scalar(scalar, digest);
    if (prefix) {
        memcpy(prefix, digest + 32, 32);
    }
    memzero(digest, sizeof(digest));
}

void K__ed25519_publickey(const K__ed25519_secret_key sk, K__ed25519_public_key pk)
{
    uint8_t scalar[32];
    expand_secret(sk, scalar, NULL);
    crypto_eddsa_scalarbase(pk, scalar);
    memzero(scalar, sizeof(scalar));
}

void K__ed25519_sign(const unsigned char *m, size_t mlen, const K__ed25519_secret_key sk,
                     const K__ed25519_public_key pk, K__ed25519_signature rs)
{
    uint8_t secret[64];
    memcpy(secret, sk, 32);
    memcpy(secret + 32, pk, 32);
    crypto_ed25519_sign(rs, secret, m, mlen);
    memzero(secret, sizeof(secret));
}

/* Hedged signing: the nonce mixes fresh randomness into the RFC 8032 derivation, as upstream does. */
int K__ed25519_randomized_sign_init(ed25519_randomized_sign_state_t *state, const K__ed25519_secret_key sk,
                                    const K__ed25519_public_key pk)
{
    static const uint8_t domain[] = "CanoKey Ed25519 randomized nonce v1";
    uint8_t prefix[32];
    uint8_t random[SHA512_DIGEST_LENGTH];
    uint8_t digest[SHA512_DIGEST_LENGTH];

    if (!state || !sk || !pk) {
        return -1;
    }
    memzero(state, sizeof(*state));
    expand_secret(sk, state->scalar, prefix);

    random_buffer(random, sizeof(random));
    sha512_init(&state->hash);
    sha512_update(&state->hash, domain, sizeof(domain) - 1);
    sha512_update(&state->hash, prefix, sizeof(prefix));
    sha512_update(&state->hash, pk, sizeof(K__ed25519_public_key));
    sha512_update(&state->hash, random, sizeof(random));
    sha512_final(&state->hash, digest);
    crypto_eddsa_reduce(state->nonce, digest);
    crypto_eddsa_scalarbase(state->encoded_r, state->nonce);

    sha512_init(&state->hash);
    sha512_update(&state->hash, state->encoded_r, sizeof(state->encoded_r));
    sha512_update(&state->hash, pk, sizeof(K__ed25519_public_key));
    state->active = 1;

    memzero(prefix, sizeof(prefix));
    memzero(random, sizeof(random));
    memzero(digest, sizeof(digest));
    return 0;
}

int K__ed25519_randomized_sign_update(ed25519_randomized_sign_state_t *state, const uint8_t *data, size_t len)
{
    if (!state || !state->active || (!data && len)) {
        return -1;
    }
    sha512_update(&state->hash, data, len);
    return 0;
}

int K__ed25519_randomized_sign_final(ed25519_randomized_sign_state_t *state, K__ed25519_signature sig)
{
    uint8_t digest[SHA512_DIGEST_LENGTH];
    uint8_t challenge[32];

    if (!state || !sig || !state->active) {
        return -1;
    }
    sha512_final(&state->hash, digest);
    crypto_eddsa_reduce(challenge, digest);
    memcpy(sig, state->encoded_r, 32);
    crypto_eddsa_mul_add(sig + 32, challenge, state->scalar, state->nonce);

    memzero(digest, sizeof(digest));
    memzero(challenge, sizeof(challenge));
    memzero(state, sizeof(*state));
    return 0;
}

void K__x25519(K__x25519_key shared_secret, const K__x25519_key private_key, const K__x25519_key public_key)
{
    crypto_x25519(shared_secret, private_key, public_key);
}

/*
 * ML-DSA-65 needs the mldsa-native backend that ships inside upstream's TF-PSA-Crypto. The CTAP
 * applet is built with CTAP_RESTRICT_ALGORITHMS (ES256 and EdDSA only), so these never run.
 */
int ml_dsa_65_keygen_streaming(uint8_t *out, size_t out_size, mldsa_keygen_state_t *state, uint8_t *tr_out)
{
    return -1;
}

void ml_dsa_65_keygen_streaming_abort(mldsa_keygen_state_t *state)
{
}

int ml_dsa_65_sign_seed_streaming(uint8_t *out, size_t out_size, mldsa_sign_state_t *state, const uint8_t *msg,
                                  size_t msg_len, const uint8_t *ctx, size_t ctx_len, const uint8_t *tr)
{
    return -1;
}

void ml_dsa_65_sign_streaming_abort(mldsa_sign_state_t *state)
{
}