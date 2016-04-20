/*struct Packet {
    
    char opcode;
    unsigned int keyLen;
    unsigned int extraLen;
    unsigned int valueLen;
    
    char *extra;
    char *key;
    char *value;
}

Packet* DecodeBuffer(char *buffer, int32_t len)
{
    unsigned int keyLen = 0, extraLen = 0, valueLen = 0, bodyLen = 0, index = 0;
    if (len < 24) return NULL;
    Packet *p = malloc(sizeof(Packat));
    if (p == NULL) return NULL;
    p->opcode = buffer[1];
    keyLen |= (buffer[3] - '0'); 
    keyLen |= ((buffer[2] - '0') << 4);
    
    extraLen |= (buffer[4] - '0');
    
    bodyLen |= (buffer[11] - '0');
    bodyLen |= ((buffer[10] - '0') << 4);
    bodyLen |= ((buffer[9] - '0') << 8);
    bodyLen |= ((buffer[8] - '0') << 12);
    
    valueLen = bodyLen - extraLen - keyLen;
    p->bodyLen = bodyLen;
    p->extraLen = extraLen;
    p->keyLen = keyLen;
    p->valueLen = valueLen;
    index = 24;
    p->extra = NULL;
    p->key = NULL;
    p->value = NULL;
    if (extraLen) {
        p->extra = malloc(extraLen);
        if (p->extra == NULL) goto err;
        memcpy(p->extra, buffer + index, extraLen);
        index += extraLen;
    } 
    if (keyLen) {
        p->key = malloc(keyLen);
        if (p->key == NULL) goto err;
        memcpy(p->key, buffer + index, keyLen);
        index += keyLen;
    }
    
    if (valueLen) {
        p->value = malloc(valueLen);
        if (p->value == NULL) goto err;
        memcpy(p->value, buffer + index, valueLen);
        index += valueLen;
    }
    return p;
err:
    free(p->extra);
    free(p->key);
    free(p->value);
    free(p);  
    return NULL;
}*/