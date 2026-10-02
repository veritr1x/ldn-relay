/* MIT. USB import only; shared key is never advertised or logged. */
#import <Foundation/Foundation.h>
#import <Security/Security.h>
static NSData *LRLoadPairingKey(void){
    NSDictionary *query=@{(__bridge id)kSecClass:(__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService:@"dev.ldn-relay.pairing.v4",(__bridge id)kSecAttrAccount:@"companion"};
    NSURL *documents=[NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    NSURL *file=[documents URLByAppendingPathComponent:@"pairing.key"];
    if([NSFileManager.defaultManager fileExistsAtPath:file.path]){
        NSData *candidate=[NSData dataWithContentsOfURL:file];
        if(candidate.length!=32)return nil; /* Bad import never downgrades to old key. */
        NSDictionary *attrs=@{(__bridge id)kSecValueData:candidate,
            (__bridge id)kSecAttrAccessible:(__bridge id)kSecAttrAccessibleWhenUnlockedThisDeviceOnly};
        OSStatus result=SecItemUpdate((__bridge CFDictionaryRef)query,(__bridge CFDictionaryRef)attrs);
        if(result==errSecItemNotFound){NSMutableDictionary *add=[query mutableCopy];[add addEntriesFromDictionary:attrs];result=SecItemAdd((__bridge CFDictionaryRef)add,NULL);}
        if(result!=errSecSuccess)return nil;
        if(![NSFileManager.defaultManager removeItemAtURL:file error:nil])return nil;
    }
    NSMutableDictionary *read=[query mutableCopy];read[(__bridge id)kSecReturnData]=@YES;
    CFTypeRef value=NULL;OSStatus result=SecItemCopyMatching((__bridge CFDictionaryRef)read,&value);
    NSData *key=CFBridgingRelease(value);return result==errSecSuccess && key.length==32?key:nil;
}
