/*
 * Copyright (c) 2018 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */


/*
 * Example code for a Secure Partition Manager application.
 * The application uses the SPM to set the security attributions of
 * the MCU resources (Flash, SRAM and Peripherals). It uses the core
 * TrustZone-M API to prepare the MCU to jump into Non-Secure firmware
 * execution.
 *
 * The following security configuration for Flash and SRAM is applied:
 *
 *                FLASH
 *  1 MB  |---------------------|
 *        |                     |
 *        |                     |
 *        |                     |
 *        |                     |
 *        |                     |
 *        |     Non-Secure      |
 *        |       Flash         |
 *        |                     |
 * 256 kB |---------------------|
 *        |                     |
 *        |     Secure          |
 *        |      Flash          |
 *  0 kB  |---------------------|
 *
 *
 *                SRAM
 * 256 kB |---------------------|
 *        |                     |
 *        |                     |
 *        |                     |
 *        |     Non-Secure      |
 *        |    SRAM (image)     |
 *        |                     |
 * 128 kB |.................... |
 *        |     Non-Secure      |
 *        |  SRAM (BSD Library) |
 *  64 kB |---------------------|
 *        |      Secure         |
 *        |       SRAM          |
 *  0 kB  |---------------------|
 */

#ifndef EMU
#include <spm.h>
#include <aarch32/cortex_m/tz.h>
#include <blst.h>
#include <sys/printk.h>
#include <secure_services.h>
#include <bl_crypto.h>
#include <common.h>
#endif


blst_scalar sk;
blst_scalar secret_keys_store[10];
blst_scalar sk_sign;
char public_keys_hex_store[960];
int keystore_size = 0;

#ifndef EMU
__TZ_NONSECURE_ENTRY_FUNC
#endif
int get_keystore_size(){
        return keystore_size;
}

#ifndef EMU
__TZ_NONSECURE_ENTRY_FUNC
#endif
void store_pk(char* public_key_hex){
        int cont = keystore_size - 1;
        for(int i = 0; i < 96; i++){
            public_keys_hex_store[i+96*cont] = public_key_hex[i];
        }
}

#ifndef EMU
__TZ_NONSECURE_ENTRY_FUNC
#endif
void getkeys(char* public_keys_hex_store_ns){
        for(int i = 0; i < keystore_size*96; i++){
            public_keys_hex_store_ns[i] = public_keys_hex_store[i];
        }
}

#ifndef EMU
__TZ_NONSECURE_ENTRY_FUNC
#endif
int pk_in_keystore(char * public_key_hex, int offset){

        int ret = 0;

        int c = 0;
        int cont = 0;

        if(keystore_size == 0){
                ret = -1;
        }

        for(int i = 0; i < keystore_size; i++){
            for(int x = 0; x < 96; x++){
                if(public_key_hex[x + offset] != public_keys_hex_store[x + cont]){
                    c = 1;
                    break;
                }
            }
            if (c == 0){
                sk_sign = secret_keys_store[i];
                break;
            } else {
                if((i+1) < keystore_size){
                    cont += 96;
                    c = 0;
                }else{
                    ret = -1;
                }
            }
        }
        return ret;
}

#ifndef EMU
__TZ_NONSECURE_ENTRY_FUNC
#endif
void ikm_sk(char* info){
        // For security, IKM MUST be infeasible to guess, e.g., generated by a trusted
        // source of randomness. IKM MUST be at least 32 bytes long, but it MAY be longer.
        unsigned char ikm[32];
#ifndef EMU
	    const int random_number_len = 144;     
        uint8_t random_number[random_number_len];
        size_t olen = random_number_len;
        int ret;

        ret = nrf_cc3xx_platform_ctr_drbg_get(NULL, random_number, random_number_len, &olen);
        
        ocrypto_sha256(ikm, random_number, random_number_len);
#else
        for(int i = 0; i < 32; i++){
            ikm[i] = rand();
        }
#endif

        
        //Secret key (256-bit scalar)
        blst_keygen(&sk, ikm, sizeof(ikm), info, sizeof(info));
        secret_keys_store[keystore_size] = sk;
        keystore_size++;
}

#ifndef EMU
__TZ_NONSECURE_ENTRY_FUNC
#endif
void sk_to_pk(blst_p1* pk){
        blst_sk_to_pk_in_g1(pk, &sk);
}

#ifndef EMU
__TZ_NONSECURE_ENTRY_FUNC
#endif
void sign_pk(blst_p2* sig, blst_p2* hash){
        blst_sign_pk_in_g1(sig, hash, &sk_sign);
}

#ifndef EMU
__TZ_NONSECURE_ENTRY_FUNC
#endif
void reset(){
        memset(secret_keys_store, 0, sizeof(secret_keys_store));
        memset(public_keys_hex_store, 0, 960);
        keystore_size = 0;
}

#ifndef EMU
__TZ_NONSECURE_ENTRY_FUNC
#endif
int import_sk(blst_scalar* sk_imp){
        int ret = 0;

        int c = 0;

        if(keystore_size == 0){
                secret_keys_store[keystore_size] = *sk_imp;
                keystore_size++;
                sk = *sk_imp;
        }else{
            for(int i = 0; i < keystore_size; i++){
                for(int x = 0; x < 32; x++){
                    if(secret_keys_store[i].b[x] != (*sk_imp).b[x]){
                        c = 1;
                        break;
                    }
            }
                if (c == 0){
                    ret = -1;
                    break;
                } else {
                    if((i+1) < keystore_size){
                        c = 0;
                    }else{
                        secret_keys_store[keystore_size] = *sk_imp;
                        keystore_size++;
                        sk = *sk_imp;
                        break;
                    }
                }
            }
        }
        return ret;
}

#ifndef EMU
void main(void)
{
	spm_config();
	spm_jump();
}
#endif