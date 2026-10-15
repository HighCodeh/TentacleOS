set(SRC libsodium/src/libsodium)
# Derived from libsodium/src/libsodium/Makefile.am
# (ignoring the !MINIMAL set)
set(srcs
    "${SRC}/crypto_aead/chacha20poly1305/aead_chacha20poly1305.c"
    "${SRC}/crypto_aead/xchacha20poly1305/aead_xchacha20poly1305.c"
    "${SRC}/crypto_aead/aegis256/aead_aegis256.c"
    "${SRC}/crypto_aead/aegis256/aegis256_soft.c"
    "${SRC}/crypto_aead/aegis128l/aead_aegis128l.c"
    "${SRC}/crypto_aead/aegis128l/aegis128l_soft.c"
    "${SRC}/crypto_auth/crypto_auth.c"
    "${SRC}/crypto_auth/hmacsha256/auth_hmacsha256.c"
    "${SRC}/crypto_auth/hmacsha512/auth_hmacsha512.c"
    "${SRC}/crypto_auth/hmacsha512256/auth_hmacsha512256.c"
    "${SRC}/crypto_box/crypto_box.c"
    "${SRC}/crypto_box/crypto_box_easy.c"
    "${SRC}/crypto_box/crypto_box_seal.c"
    "${SRC}/crypto_box/curve25519xchacha20poly1305/box_curve25519xchacha20poly1305.c"
    "${SRC}/crypto_box/curve25519xchacha20poly1305/box_seal_curve25519xchacha20poly1305.c"
    "${SRC}/crypto_box/curve25519xsalsa20poly1305/box_curve25519xsalsa20poly1305.c"
    "${SRC}/crypto_core/ed25519/core_ed25519.c"
    "${SRC}/crypto_core/ed25519/core_ristretto255.c"
    "${SRC}/crypto_core/ed25519/ref10/ed25519_ref10.c"
    "${SRC}/crypto_core/hchacha20/core_hchacha20.c"
    "${SRC}/crypto_core/hsalsa20/core_hsalsa20.c"
    "${SRC}/crypto_core/hsalsa20/ref2/core_hsalsa20_ref2.c"
    "${SRC}/crypto_core/keccak1600/keccak1600.c"
    "${SRC}/crypto_core/keccak1600/ref/keccak1600_ref.c"
    "${SRC}/crypto_core/salsa/ref/core_salsa_ref.c"
    "${SRC}/crypto_core/softaes/softaes.c"
    "${SRC}/crypto_generichash/blake2b/generichash_blake2.c"
    "${SRC}/crypto_generichash/blake2b/ref/blake2b-compress-avx2.c"
    "${SRC}/crypto_generichash/blake2b/ref/blake2b-compress-ref.c"
    "${SRC}/crypto_generichash/blake2b/ref/blake2b-compress-ssse3.c"
    "${SRC}/crypto_generichash/blake2b/ref/blake2b-ref.c"
    "${SRC}/crypto_generichash/blake2b/ref/generichash_blake2b.c"
    "${SRC}/crypto_generichash/crypto_generichash.c"
    "${SRC}/crypto_ipcrypt/crypto_ipcrypt.c"
    "${SRC}/crypto_ipcrypt/ipcrypt_soft.c"
    "${SRC}/crypto_hash/crypto_hash.c"
    "${SRC}/crypto_hash/sha256/hash_sha256.c"
    "${SRC}/crypto_hash/sha512/hash_sha512.c"
    "${SRC}/crypto_hash/sha3/hash_sha3.c"
    "${SRC}/crypto_kdf/blake2b/kdf_blake2b.c"
    "${SRC}/crypto_kdf/crypto_kdf.c"
    "${SRC}/crypto_kdf/hkdf/kdf_hkdf_sha256.c"
    "${SRC}/crypto_kdf/hkdf/kdf_hkdf_sha512.c"
    "${SRC}/crypto_kem/crypto_kem.c"
    "${SRC}/crypto_kem/mlkem768/kem_mlkem768.c"
    "${SRC}/crypto_kem/mlkem768/ref/kem_mlkem768_ref.c"
    "${SRC}/crypto_kem/xwing/kem_xwing.c"
    "${SRC}/crypto_kx/crypto_kx.c"
    "${SRC}/crypto_onetimeauth/crypto_onetimeauth.c"
    "${SRC}/crypto_onetimeauth/poly1305/donna/poly1305_donna.c"
    "${SRC}/crypto_onetimeauth/poly1305/onetimeauth_poly1305.c"
    "${SRC}/crypto_pwhash/argon2/argon2-core.c"
    "${SRC}/crypto_pwhash/argon2/argon2-encoding.c"
    "${SRC}/crypto_pwhash/argon2/argon2-fill-block-avx2.c"
    "${SRC}/crypto_pwhash/argon2/argon2-fill-block-avx512f.c"
    "${SRC}/crypto_pwhash/argon2/argon2-fill-block-ref.c"
    "${SRC}/crypto_pwhash/argon2/argon2-fill-block-ssse3.c"
    "${SRC}/crypto_pwhash/argon2/argon2.c"
    "${SRC}/crypto_pwhash/argon2/blake2b-long.c"
    "${SRC}/crypto_pwhash/argon2/pwhash_argon2i.c"
    "${SRC}/crypto_pwhash/argon2/pwhash_argon2id.c"
    "${SRC}/crypto_pwhash/crypto_pwhash.c"
    "${SRC}/crypto_pwhash/scryptsalsa208sha256/crypto_scrypt-common.c"
    "${SRC}/crypto_pwhash/scryptsalsa208sha256/nosse/pwhash_scryptsalsa208sha256_nosse.c"
    "${SRC}/crypto_pwhash/scryptsalsa208sha256/pbkdf2-sha256.c"
    "${SRC}/crypto_pwhash/scryptsalsa208sha256/pwhash_scryptsalsa208sha256.c"
    "${SRC}/crypto_pwhash/scryptsalsa208sha256/scrypt_platform.c"
    "${SRC}/crypto_scalarmult/crypto_scalarmult.c"
    "${SRC}/crypto_scalarmult/curve25519/ref10/x25519_ref10.c"
    "${SRC}/crypto_scalarmult/curve25519/sandy2x/consts.S"
    "${SRC}/crypto_scalarmult/curve25519/sandy2x/curve25519_sandy2x.c"
    "${SRC}/crypto_scalarmult/curve25519/sandy2x/fe51_invert.c"
    "${SRC}/crypto_scalarmult/curve25519/sandy2x/fe51_mul.S"
    "${SRC}/crypto_scalarmult/curve25519/sandy2x/fe51_nsquare.S"
    "${SRC}/crypto_scalarmult/curve25519/sandy2x/fe51_pack.S"
    "${SRC}/crypto_scalarmult/curve25519/sandy2x/fe_frombytes_sandy2x.c"
    "${SRC}/crypto_scalarmult/curve25519/sandy2x/ladder.S"
    "${SRC}/crypto_scalarmult/curve25519/sandy2x/sandy2x.S"
    "${SRC}/crypto_scalarmult/curve25519/scalarmult_curve25519.c"
    "${SRC}/crypto_scalarmult/ed25519/ref10/scalarmult_ed25519_ref10.c"
    "${SRC}/crypto_scalarmult/ristretto255/ref10/scalarmult_ristretto255_ref10.c"
    "${SRC}/crypto_secretbox/crypto_secretbox.c"
    "${SRC}/crypto_secretbox/crypto_secretbox_easy.c"
    "${SRC}/crypto_secretbox/xchacha20poly1305/secretbox_xchacha20poly1305.c"
    "${SRC}/crypto_secretbox/xsalsa20poly1305/secretbox_xsalsa20poly1305.c"
    "${SRC}/crypto_secretstream/xchacha20poly1305/secretstream_xchacha20poly1305.c"
    "${SRC}/crypto_shorthash/crypto_shorthash.c"
    "${SRC}/crypto_shorthash/siphash24/ref/shorthash_siphash24_ref.c"
    "${SRC}/crypto_shorthash/siphash24/ref/shorthash_siphashx24_ref.c"
    "${SRC}/crypto_shorthash/siphash24/shorthash_siphash24.c"
    "${SRC}/crypto_shorthash/siphash24/shorthash_siphashx24.c"
    "${SRC}/crypto_sign/crypto_sign.c"
    "${SRC}/crypto_sign/ed25519/ref10/keypair.c"
    "${SRC}/crypto_sign/ed25519/ref10/obsolete.c"
    "${SRC}/crypto_sign/ed25519/ref10/open.c"
    "${SRC}/crypto_sign/ed25519/ref10/sign.c"
    "${SRC}/crypto_sign/ed25519/sign_ed25519.c"
    "${SRC}/crypto_stream/chacha20/dolbeau/chacha20_dolbeau-avx2.c"
    "${SRC}/crypto_stream/chacha20/dolbeau/chacha20_dolbeau-ssse3.c"
    "${SRC}/crypto_stream/chacha20/ref/chacha20_ref.c"
    "${SRC}/crypto_stream/chacha20/stream_chacha20.c"
    "${SRC}/crypto_stream/crypto_stream.c"
    "${SRC}/crypto_stream/salsa20/ref/salsa20_ref.c"
    "${SRC}/crypto_stream/salsa20/stream_salsa20.c"
    "${SRC}/crypto_stream/salsa20/xmm6/salsa20_xmm6-asm.S"
    "${SRC}/crypto_stream/salsa20/xmm6/salsa20_xmm6.c"
    "${SRC}/crypto_stream/salsa20/xmm6int/salsa20_xmm6int-avx2.c"
    "${SRC}/crypto_stream/salsa20/xmm6int/salsa20_xmm6int-sse2.c"
    "${SRC}/crypto_stream/salsa2012/ref/stream_salsa2012_ref.c"
    "${SRC}/crypto_stream/salsa2012/stream_salsa2012.c"
    "${SRC}/crypto_stream/salsa208/ref/stream_salsa208_ref.c"
    "${SRC}/crypto_stream/salsa208/stream_salsa208.c"
    "${SRC}/crypto_stream/xchacha20/stream_xchacha20.c"
    "${SRC}/crypto_stream/xsalsa20/stream_xsalsa20.c"
    "${SRC}/crypto_verify/verify.c"
    "${SRC}/crypto_xof/shake128/xof_shake128.c"
    "${SRC}/crypto_xof/shake128/ref/shake128_ref.c"
    "${SRC}/crypto_xof/shake256/xof_shake256.c"
    "${SRC}/crypto_xof/shake256/ref/shake256_ref.c"
    "${SRC}/crypto_xof/turboshake128/xof_turboshake128.c"
    "${SRC}/crypto_xof/turboshake128/ref/turboshake128_ref.c"
    "${SRC}/crypto_xof/turboshake256/xof_turboshake256.c"
    "${SRC}/crypto_xof/turboshake256/ref/turboshake256_ref.c"
    "${SRC}/randombytes/randombytes.c"
    "${SRC}/sodium/codecs.c"
    "${SRC}/sodium/core.c"
    "${SRC}/sodium/runtime.c"
    "${SRC}/sodium/utils.c"
    "${SRC}/sodium/version.c"
    "port/randombytes_esp32.c")

if(CONFIG_LIBSODIUM_USE_MBEDTLS_SHA)
    list(APPEND srcs
        "port/crypto_hash_mbedtls/crypto_hash_sha256_mbedtls.c"
        "port/crypto_hash_mbedtls/crypto_hash_sha512_mbedtls.c")
    set(include_dirs port_include ${SRC}/include)
    set(priv_include_dirs port port_include/sodium ${SRC}/include/sodium)
else()
    list(APPEND srcs
        "${SRC}/crypto_hash/sha256/cp/hash_sha256_cp.c"
        "${SRC}/crypto_hash/sha512/cp/hash_sha512_cp.c")
    set(include_dirs ${SRC}/include port_include)
    set(priv_include_dirs ${SRC}/include/sodium port_include/sodium port)
endif()

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "${include_dirs}"
                    PRIV_INCLUDE_DIRS "${priv_include_dirs}"
                    REQUIRES mbedtls)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    CONFIGURED
    NATIVE_LITTLE_ENDIAN
    HAVE_WEAK_SYMBOLS
    __STDC_LIMIT_MACROS
    __STDC_CONSTANT_MACROS
    __STDC_WANT_LIB_EXT1__=0
    )

# patch around warnings in third-party files
set_source_files_properties(
    ${SRC}/crypto_pwhash/argon2/pwhash_argon2i.c
    ${SRC}/crypto_pwhash/argon2/pwhash_argon2id.c
    ${SRC}/crypto_pwhash/argon2/argon2-core.c
    ${SRC}/crypto_pwhash/scryptsalsa208sha256/pwhash_scryptsalsa208sha256.c
    PROPERTIES COMPILE_FLAGS
    -Wno-type-limits
    )
set_source_files_properties(
    ${SRC}/sodium/utils.c
    PROPERTIES COMPILE_FLAGS
    -Wno-unused-variable
    )
set_source_files_properties(
    ${SRC}/crypto_pwhash/argon2/argon2-fill-block-ref.c
    PROPERTIES COMPILE_FLAGS
    -Wno-unknown-pragmas
    )

# Temporary suppress "fallthrough" warnings until they are fixed in libsodium repo
set_source_files_properties(
    ${SRC}/crypto_shorthash/siphash24/ref/shorthash_siphashx24_ref.c
    ${SRC}/crypto_shorthash/siphash24/ref/shorthash_siphash24_ref.c
    PROPERTIES COMPILE_FLAGS
    -Wno-implicit-fallthrough
    )

# Upstream libsodium 1.0.22 KEM sources define parameters as plain pointers
# while their headers declare them as fixed-size arrays, which trips
# -Werror=array-parameter on GCC >= 11. Suppress until fixed upstream.
set_source_files_properties(
    ${SRC}/crypto_kem/mlkem768/kem_mlkem768.c
    ${SRC}/crypto_kem/xwing/kem_xwing.c
    PROPERTIES COMPILE_FLAGS
    -Wno-array-parameter
    )

set_source_files_properties(
    ${SRC}/randombytes/randombytes.c
    PROPERTIES COMPILE_FLAGS
    -DRANDOMBYTES_DEFAULT_IMPLEMENTATION
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function)

if(CONFIG_LIBSODIUM_USE_MBEDTLS_SHA)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_SOURCE_DIR}/port_include/sodium/crypto_hash_sha256.h>"
        "$<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_SOURCE_DIR}/port_include/sodium/crypto_hash_sha512.h>")
endif()

if(CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_DISABLE)
    # some libsodium variables are only used for asserts
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-but-set-variable)
endif()

if(CONFIG_COMPILER_STATIC_ANALYZER AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # GCC's -fanalyzer causes the compiler to hang on libsodium's elliptic curve code
    # due to combinatorial state explosion in the interprocedural analysis
    target_compile_options(${COMPONENT_LIB} PRIVATE "-fno-analyzer")
endif()
