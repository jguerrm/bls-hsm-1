/*
    A library to handle http requests to the remote signer

    IMPORTANT comment about MAXHeaders and MAXKeys(maximum keystore)

    Ask about maximum size of an ethereum signature
*/

#ifndef httpRemote_h
#define httpRemote_h

#include "./picohttpparser.h"
#include "./cJSON.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "./common.h"
#include "./bls_hsm_ns.h"

#define SCRYPTTYPE 1
#define PBKDF2TYPE 2

#define signatureOffset 12//due to  Signature: \n

#define MAXSizeEthereumSignature 192
#define MAXBUF 32768
#define MAXHeaders 100
#define MAXKeys 10 //Maximum numbers of keys to store
#define keySize 96

#define sign 0
#define upcheck 1
#define getKeys 2
#define importKey 4

#define textPlain 0
#define applicationJson 1

char upcheckStr[] = "/upcheck";
char getKeysStr[] = "/api/v1/eth2/publicKeys";
char signRequestStr[] = "/api/v1/eth2/sign/0x";
char contentLengthStr[] = "content-length";
char keymanagerStr[] = "/eth/v1/keystores";
char acceptStr[] = "Accept";

char textPlainStr[] = "text/plain";
char applicationJsonStr[] = "application/json";

/*
**********************************************************RESPONSES****************************************************************
*/
char upcheckResponse[] = "HTTP/1.1 200 OK\r\n"
   "content-type: text/p"
   "lain; charset=utf-8"
   "\r\n"
   "content-length: 0\r\n\r\n";

/*
We got to add later the size of the json in text, 2 \n and the json with the publick Keys
json format: ["0xkey", "0xkey", ....]
keys are in hex
*/
char getKeysResponse[] = "HTTP/1.1 200 OK\r\n"
   "Content-Type: application/json"
   "\r\n"
   "Content-Length: ";

/*
We got to add later the size of the signature, 2 \n and the signature
signature format 0xsignature
signature is in hex
*/
char signResponse[] = "HTTP/1.1 200 OK\r\n"
   "Content-Type: application/json"
   "\r\n"
   "Content-Length: ";

char signResponseText[] = "HTTP/1.1 200 OK\r\n"
   "Content-Type: text/plain"
   "\r\n"
   "Content-Length: ";

char* badRequest = "HTTP/1.1 400 Bad request format\r\n"
"Content-Type: application/json\r\n"
"Content-Length: 0\r\n\r\n";

char* pknf = "HTTP/1.1 404 Public key not found\r\n"
"Content-Type: application/json\r\n"
"Content-Length: 0\r\n\r\n";

/*
************************************************************************************************************************************
*/

struct boardRequest{
    int method; //Board
    int acceptType;
    char* json;
    char* keyToSign;//Size is always of keySize bytes
    char publicKeys[MAXKeys][keySize];//In hex
    int nKeys;//number of keys
    int jsonLen;//In fact we won't need this field because there will be a \0 at the end of the json, but just in case 
};

struct httpRequest{
    char* method;
    char* path;
    char* body;
    int acceptType;
    int requestLen;
    size_t methodLen;
    size_t pathLen;
    size_t bodyLen;
    int minorVersion;
    struct phr_header headers[MAXHeaders];//We can only process this number of headers
    size_t numHeaders;   
};

/*
    On success returns 0
    On empty keystore returns -1
*/
int copyKeys(struct boardRequest* request){
    int ksize = get_keystore_size();
    if(ksize == 0){
        request->nKeys = 0;
        return -1;
    }else{
        char buffer[ksize][96];
        getkeys(buffer);
        request->nKeys = 0;
        for(int i = 0; i < ksize; i++){
            ++(request->nKeys);
            strncpy(request->publicKeys[i], buffer[i], 96);
        }
    }

    return 0;
}

void getAcceptOptions(struct httpRequest* request){
    int acceptTypePosition = 0;
    int acceptStrSize = strlen(acceptStr);

    //6 is the size of Accept
    for(acceptTypePosition = 0;
    (acceptTypePosition < (int) request->numHeaders) && (request->headers[acceptTypePosition].name != NULL) &&
    !((request->headers[acceptTypePosition].name_len == acceptStrSize) 
    && !(strncmp(acceptStr, request->headers[acceptTypePosition].name, acceptStrSize))); 
    ++acceptTypePosition){}

    if(acceptTypePosition < (int) request->numHeaders){
        if((request->headers[acceptTypePosition].value_len == (int) strlen(applicationJsonStr)) &&
        (strncmp(applicationJsonStr, request->headers[acceptTypePosition].value, strlen(applicationJsonStr)) == 0) || 
        (request->headers[acceptTypePosition].value_len == 3) &&
        (strncmp("*/*", request->headers[acceptTypePosition].value, 3) == 0)){
            request->acceptType = applicationJson;
        }else{//If the Accept type is not application/json or */* we select the simplest one
            request->acceptType = textPlain;
        }
    }else{//If there is not an explicit accept header send response as text/plain
        request->acceptType = textPlain;
    }
}

void getBody(char* buffer, size_t bufferSize, struct httpRequest* request){
    int bodyLengthPosition;//Where is content-length in request->headers
    int contentLengthStrSize = strlen(contentLengthStr);

    //14 is the size of content-length
    for(bodyLengthPosition = 0; 
    (bodyLengthPosition < (int) request->numHeaders) && (request->headers[bodyLengthPosition].name != NULL) &&
    !((request->headers[bodyLengthPosition].name_len == contentLengthStrSize) 
    && (strncmp(contentLengthStr, request->headers[bodyLengthPosition].name, contentLengthStrSize))); 
    ++bodyLengthPosition){}

    if(bodyLengthPosition < request->numHeaders){
        char bodyLenChar[(int) request->headers[bodyLengthPosition].value_len + 1];
        strncpy(bodyLenChar, request->headers[bodyLengthPosition].value, request->headers[bodyLengthPosition].value_len);
        bodyLenChar[(int) request->headers[bodyLengthPosition].value_len] = '\0';

        request->bodyLen = (size_t) atoi(bodyLenChar);

        if((int) request->bodyLen > 0){
            request->body = buffer + (int) bufferSize - (int) request->bodyLen;
            //printf("%s", request->body);
        }else{
            request->body = NULL;
        }
    }
}

/*
    On succes returns 0
    On error returns -1
*/
int checkKey(struct boardRequest* request){
    if(copyKeys(request) == -1){
        return -1;
    }else{
        for(int i = 0; i < request->nKeys; ++i){
            if(strncmp(request->keyToSign, request->publicKeys[i], keySize) == 0){ 
                return 0;
            }
        }
    }

    return -1;
}

/*   
    On succes returns 0
    On error returns -1
    On incomplete request returns -2
    On bad format returns -3
    We are only going to support GET and POST requests, so if we get another type of request we are going to handle it like an error.
*/
int parseRequest(char* buffer, size_t bufferSize, struct boardRequest* reply){//boardRequest out, buffer in

    if((strncmp(buffer, "POST", 4) != 0) && (strncmp(buffer, "GET", 3) != 0)){
        return -1;
    }else if (strncmp(buffer, "POST", 4) == 0){
        char* p = strstr(buffer, "Content-Length: ");
        if(p != NULL){
            char* q = strchr(p, '\r');
            if(q != NULL){
                int clen = atoi(p + 16);
                int headlen = q - (char*) buffer;
                int explen = clen + 4 + headlen;
                if(bufferSize > explen){
                    return -1;
                }else if (bufferSize < explen){
                    return -2;
                }
            }else{
                if(bufferSize < 300){ // Arbitrary limit to discard request
                    return -2;
                }
                return -1;
            }
        }else{
            if(bufferSize < 300){ // Arbitrary limit to discard request
                return -2;
            }
        }
    }else{
        char* p = strstr(buffer, "\r\n\r\n");
        if(p == NULL){
            if(bufferSize < 300){ // Arbitrary limit to discard request
                return -2;
            }
            return -1;
        }
    }
    buffer[bufferSize] = '\0';
    struct httpRequest request;

    request.requestLen = phr_parse_request(buffer, bufferSize, (const char**) &(request.method), &(request.methodLen), 
    (const char**) &(request.path), &(request.pathLen), &(request.minorVersion), request.headers, &(request.numHeaders), 0);

    if(request.requestLen < 0){
        return request.requestLen;
    }

    if((request.methodLen == 3) && (strncmp(request.method, "GET", 3) == 0)){
        if((request.pathLen == strlen(upcheckStr)) && (strncmp(request.path, upcheckStr, strlen(upcheckStr)) == 0)){
            reply->method = upcheck;
        }else if((request.pathLen == strlen(getKeysStr)) && (strncmp(request.path, getKeysStr, strlen(getKeysStr)) == 0)){
            reply->method = getKeys;
        }else{
            return -3;
        }
    }else if((request.methodLen == 4) && (strncmp(request.method, "POST", 4) == 0)){
        getBody(buffer, bufferSize, &request);
        if((request.pathLen == (strlen(signRequestStr) + keySize)) && (strncmp(request.path, signRequestStr, strlen(signRequestStr)) == 0)){
            reply->keyToSign[0] = '0';
            reply->keyToSign[1] = 'x';
            strncpy(reply->keyToSign + 2, request.path + strlen(signRequestStr), keySize);
            reply->keyToSign[keySize + 2] = '\0';
                        
            reply->json = request.body;
            reply->jsonLen = request.bodyLen;

            reply->method = sign;        
        }else if((request.pathLen == strlen(keymanagerStr)) && (strncmp(request.path, keymanagerStr, strlen(keymanagerStr)) == 0)){
            reply->json = request.body;
            reply->jsonLen = request.bodyLen;

            reply->method = importKey;
        }else{
            return -3;
        }
    }else{
        return -3;
    }

    return 0;
}

/*
    Returns size of buffer
*/
int upcheckResponseStr(char* buffer){
    strcpy(buffer, upcheckResponse);
    return strlen(upcheckResponse);
}

/*
    Returns size of buffer
*/
int pknotfoundResponseStr(char* buffer){
    strcpy(buffer, pknf);
    return strlen(pknf);
}

/*
    Returns size of buffer
*/
int getKeysResponseStr(char* buffer, struct boardRequest* request){
    int jsonKeysSize = 6*request->nKeys - 1 + request->nKeys*keySize + 3;
    if(request->nKeys == 0){
        jsonKeysSize = 3;
    }

    strcpy(buffer, getKeysResponse);

    char nKeysStr[100];
    sprintf(nKeysStr, "%d", jsonKeysSize);

    strcat(buffer, nKeysStr);
    strcat(buffer, "\n\n[\n");

    for(int i = 0; i < request->nKeys; ++i){
        strcat(buffer, "\"0x");
        strncat(buffer, request->publicKeys[i], keySize);
        strcat(buffer, "\"");
        if(i + 1 < request->nKeys){
            strcat(buffer, ",");
        }
        strcat(buffer, "\n");
    }
    strcat(buffer, "]");

    return strlen(buffer);
}

/*
    Returns size of buffer or -1 on error
*/
int signResponseStr(char* buffer, struct boardRequest* request){
    cJSON* json = cJSON_Parse(request->json);
    if(json == NULL){
        return -1;
    }

    cJSON* signingroot = cJSON_GetObjectItemCaseSensitive(json, "signingRoot");
    if((signingroot == NULL) || (signingroot->valuestring == NULL)){
        return -1;
    }

    char* key = strndup(request->keyToSign, 96);
    char signat[MAXSizeEthereumSignature];//¿Maximum size of ethereum siganture?
    if(signature(key, signingroot->valuestring, signat) != 0){
        return -1;
    }

    char reply[256] = "";
    switch(request->acceptType){
        case textPlain:
            strcat(reply, "0x");
            strncat(reply, signat, MAXSizeEthereumSignature);
            strcpy(buffer, signResponseText);
            break;
        case applicationJson:
            strcat(reply, "{\"signature\": \"0x");
            strncat(reply, signat, 192);
            strcat(reply, "\"}");
            strcpy(buffer, signResponse);
            break;
        default:
            return -1;
    }

    int signatureLen = strlen(reply);
    char signatureLenStr[100];
    sprintf(signatureLenStr, "%d", signatureLen);

    strcat(buffer, signatureLenStr);
    strcat(buffer, "\n\n");
    strcat(buffer, reply);

    return strlen(buffer);
}

/*
    Returns 0 on succed and type type on type
    Returns error number on error
*/
int get_decryption_key_encryption_type(cJSON* keystore, int* type){
    if(keystore == NULL || keystore->type != cJSON_Object){
        return BADJSONFORMAT;
    }

    cJSON* crypto = cJSON_GetObjectItemCaseSensitive(keystore, "crypto");
    if(crypto == NULL){
        return BADJSONFORMAT;
    }

    cJSON* kdf = cJSON_GetObjectItemCaseSensitive(crypto, "kdf");
    if(kdf == NULL){
        return BADJSONFORMAT;
    }

    cJSON* json_function = cJSON_GetObjectItemCaseSensitive(kdf, "function");
    if(json_function == NULL || json_function->type != cJSON_String){
        return BADJSONFORMAT;
    }
    char* function = json_function->valuestring;

    if(strcmp("pbkdf2", function) == 0){
        *type = PBKDF2TYPE;
    }else if(strcmp("scrypt", function) == 0){
        *type = SCRYPTTYPE;
    }else{
        return -1;//ERROR
    }

    return 0;
}

/*
    Returns 0 on succes
    error number on error
*/
int get_decryption_key_pbkdf2_params(cJSON* keystore, char* password, unsigned char* decryption_key){
    if(keystore == NULL || keystore->type != cJSON_Object){
        return BADJSONFORMAT;
    }

    cJSON* crypto = cJSON_GetObjectItemCaseSensitive(keystore, "crypto");
    if(crypto == NULL){
        return BADJSONFORMAT;
    }

    cJSON* kdf = cJSON_GetObjectItemCaseSensitive(crypto, "kdf");
    if(kdf == NULL){
        return BADJSONFORMAT;
    }

    cJSON* params = cJSON_GetObjectItemCaseSensitive(kdf, "params");
    if(params == NULL){
        return BADJSONFORMAT;
    }

    //dklen
    cJSON* json_dklen;
    json_dklen = cJSON_GetObjectItemCaseSensitive(params, "dklen");
    if(json_dklen == NULL || json_dklen->type != cJSON_Number){
        return BADJSONFORMAT;
    }
    int dklen = json_dklen->valueint;

    //c
    cJSON* json_c;
    json_c = cJSON_GetObjectItemCaseSensitive(params, "c");
    if(json_c == NULL || json_c->type != cJSON_Number){
        return BADJSONFORMAT;
    }
    int c = json_c->valueint;

    //prf
    cJSON* json_prf = cJSON_GetObjectItemCaseSensitive(params, "prf");
    if(json_prf == NULL || json_prf->type != cJSON_String){
        return BADJSONFORMAT;
    }
    char* prf = json_prf->valuestring;

    //salt
    cJSON* json_salt_str = cJSON_GetObjectItemCaseSensitive(params, "salt");
    if(json_salt_str == NULL || json_salt_str->type != cJSON_String){
        return BADJSONFORMAT;
    }
    char* salt_str = json_salt_str->valuestring;

    //return get_decryption_key_pbkdf2(password, dklen, c, prf, salt_str, decryption_key);
    return 0;
}

/*
    Returns 0 on succes
    error number on succes
*/

int get_decryption_key_scrypt_params(cJSON* keystore, char* password, unsigned char* decryption_key){
    if(keystore == NULL || keystore->type != cJSON_Object){
        return BADJSONFORMAT;
    }

    cJSON* crypto = cJSON_GetObjectItemCaseSensitive(keystore, "crypto");
    if(crypto == NULL){
        return BADJSONFORMAT;
    }

    cJSON* kdf = cJSON_GetObjectItemCaseSensitive(crypto, "kdf");
    if(kdf == NULL){
        return BADJSONFORMAT;
    }

    cJSON* params = cJSON_GetObjectItemCaseSensitive(kdf, "params");
    if(params == NULL){
        return BADJSONFORMAT;
    }

    //dklen
    cJSON* json_dklen;
    json_dklen = cJSON_GetObjectItemCaseSensitive(params, "dklen");
    if(json_dklen == NULL || json_dklen->type != cJSON_Number){
        return BADJSONFORMAT;
    }
    int dklen = json_dklen->valueint;

    //n -> cost
    cJSON* json_n;
    json_n = cJSON_GetObjectItemCaseSensitive(params, "n");
    if(json_n == NULL || json_n->type != cJSON_Number){
        return BADJSONFORMAT;
    }
    int n = json_n->valueint;

    //r -> blockSize
    cJSON* json_r = cJSON_GetObjectItemCaseSensitive(params, "r");
    if(json_r == NULL || json_r->type != cJSON_Number){
        return BADJSONFORMAT;
    }
    int r = json_r->valueint;

    //p -> parallel
    cJSON* json_p =  cJSON_GetObjectItemCaseSensitive(params, "p");
    if(json_p == NULL || json_p->type != cJSON_Number){
        return BADJSONFORMAT;
    }
    int p = cJSON_GetObjectItemCaseSensitive(params, "p")->valueint;

    //salt
    cJSON* json_salt_str = cJSON_GetObjectItemCaseSensitive(params, "salt");
    if(json_salt_str == NULL || json_salt_str->type != cJSON_String){
        return BADJSONFORMAT;
    }
    char* salt_str = json_salt_str->valuestring;

    return get_decryption_key_scrypt(password, dklen, n, r, p, salt_str, decryption_key);

}

/*
    Returns 0 on succes
    error number on error
*/
int verificate_password_params(cJSON* keystore, unsigned char* decryption_key){
    if(keystore == NULL || keystore->type != cJSON_Object){
        return BADJSONFORMAT;
    }

    cJSON* crypto = cJSON_GetObjectItemCaseSensitive(keystore, "crypto");
    if(crypto == NULL){
        return BADJSONFORMAT;
    }

    cJSON* checksum = cJSON_GetObjectItemCaseSensitive(crypto, "checksum");
    if(checksum == NULL){
        return BADJSONFORMAT;
    }

    cJSON* cipher = cJSON_GetObjectItemCaseSensitive(crypto, "cipher");
    if(cipher == NULL){
        return BADJSONFORMAT;
    }

    //checksum_message
    cJSON* json_checksum_message = cJSON_GetObjectItemCaseSensitive(checksum, "message");
    if(json_checksum_message == NULL || json_checksum_message->type != cJSON_String){
        return BADJSONFORMAT;
    }
    char* checksum_message_hex = json_checksum_message->valuestring;

    //cipher_message
    cJSON* json_message_cipher = cJSON_GetObjectItemCaseSensitive(cipher, "message");
    if(json_message_cipher == NULL || json_message_cipher->type != cJSON_String){
        return BADJSONFORMAT;
    }
    char* cipher_message_hex = json_message_cipher->valuestring;

    return verificate_password(checksum_message_hex, cipher_message_hex, decryption_key);
}

/*
    Returns 0 on succes
    error number on error
*/
int get_private_key_params(cJSON* keystore, unsigned char* decryption_key, char* private_key){
    if(keystore == NULL || keystore->type != cJSON_Object){
        return BADJSONFORMAT;
    }

    cJSON* crypto = cJSON_GetObjectItemCaseSensitive(keystore, "crypto");
    if(crypto == NULL){
        return BADJSONFORMAT;
    }

    cJSON* cipher = cJSON_GetObjectItemCaseSensitive(crypto, "cipher");
    if(cipher == NULL){
        return BADJSONFORMAT;
    }

    cJSON* params = cJSON_GetObjectItemCaseSensitive(cipher, "params");
    if(params == NULL){
        return BADJSONFORMAT;
    }

    cJSON* json_iv_str = cJSON_GetObjectItemCaseSensitive(params, "iv");
    if(json_iv_str == NULL || json_iv_str->type != cJSON_String){
        return BADJSONFORMAT;
    }
    char* iv_str = json_iv_str->valuestring;

    cJSON* json_cipher_message = cJSON_GetObjectItemCaseSensitive(cipher, "message");
    if(json_cipher_message == NULL || json_cipher_message->type != cJSON_String){
        return BADJSONFORMAT;
    }
    char* cipher_message = json_cipher_message->valuestring;

    return get_private_key(cipher_message, iv_str, decryption_key, private_key);
}

/*
    Returns 0 on succes
    error number on error
*/
int import_from_keystore(cJSON* keystores[], char** passwords, int nKeys){
    int error;
    unsigned char decryption_key[32];
    char private_key[32];
    for(int i = 0; i < nKeys; ++i){
        int type;

        if((error = get_decryption_key_encryption_type(keystores[i], &type)) != 0){
            return error;
        }

/***********************************************************************************************************************************************
***********************************************************DECRYPTIONKEY************************************************************************
************************************************************************************************************************************************/
        if(type == PBKDF2TYPE){
            if((error = get_decryption_key_pbkdf2_params(keystores[i], passwords[i], decryption_key)) != 0){
                return error;
            }
        }else if(type == SCRYPTTYPE){
            if((error = get_decryption_key_scrypt_params(keystores[i], passwords[i], decryption_key)) != 0){
                return error;
            }
        }else{
            return -1; //ERROR
        }

/***********************************************************************************************************************************************
**************************************************************VERIFICATEPASSWORD****************************************************************
***********************************************************************************************************************************************/
        if((error = verificate_password_params(keystores[i], decryption_key)) != 0){
            return error;
        }

/***********************************************************************************************************************************************
*****************************************************************PRIVATEKEY********************************************************************* 
***********************************************************************************************************************************************/
        if((error = get_private_key_params(keystores[i], decryption_key, private_key)) != 0){
            return error;
        }
    }
    
    return 0;
}

/*
    Returns 0 on success
    -1 on error and set the variable errno to the type of error
*/

int httpImportFromKeystore(char* body){
    cJSON* json= cJSON_Parse(body);
    if(json == NULL){
        return -1;
    }

    cJSON* keystoresJson = cJSON_GetObjectItemCaseSensitive(json, "keystores");
    if((keystoresJson == NULL) || (keystoresJson->child == NULL)){
        return -1;
    }

    cJSON* passwordsJson = cJSON_GetObjectItemCaseSensitive(json, "passwords");
    if((passwordsJson == NULL) || (passwordsJson->child == NULL)){
        return -1;
    }

    int nKeysAlreadyStored = get_keystore_size();

    int nKeystores = 0;
    int nPasswords = 0;

    keystoresJson = keystoresJson->child;
    passwordsJson = passwordsJson->child;

    cJSON* keystores[MAXKeys];
    char* passwords[MAXKeys];

    while(keystoresJson != NULL){
        if(nKeystores < (MAXKeys + nKeysAlreadyStored)){
            keystores[nKeystores] = keystoresJson;
            ++nKeystores;
            keystoresJson = keystoresJson->next;
        }else{
            return -1;
        }
    }

    while(passwordsJson != NULL){
        if(nPasswords < (MAXKeys + nKeysAlreadyStored)){
            if(passwordsJson->type != cJSON_String){
                return -1;
            }else{
                passwords[nPasswords] = passwordsJson->valuestring;
            }
            ++nPasswords;
            passwordsJson = passwordsJson->next;
        }else{
            return -1;
        }
    }

    if(nKeystores != nPasswords){
        return -1;
    }

    if(import_from_keystore(keystores, (char**) passwords, nKeystores) != 0){
        return -1;
    }

    return 0;
}

/*
    On succes returns the number of bytes in buffer
    On error retuns -1
*/
int dumpHttpResponse(char* buffer, struct boardRequest* request){//boardRequest in, buffer out
    int error;
    switch(request->method){
        case sign:
            if(checkKey(request) == -1){
                return pknotfoundResponseStr(buffer);
            }
            return signResponseStr(buffer, request);
            break;
        case upcheck:
            return upcheckResponseStr(buffer);
            break;
        case getKeys:
            copyKeys(request);
            return getKeysResponseStr(buffer, request);
            break;
        case importKey:
            if((error = httpImportFromKeystore(request->json)) == 0){
                return getKeysResponseStr(buffer, request);
            }else{
                return error;
            }
            break;
        default:
            return -1;
    }
}

#endif