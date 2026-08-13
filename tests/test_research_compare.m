@import Foundation;

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "compare.h"

static NSString *report(NSString *label, unsigned int mode, unsigned int brightness, unsigned int maximum,
                        NSString *provider, BOOL include_mode, BOOL unstable) {
    NSString *mode_read = include_mode ? [NSString stringWithFormat:
        @"{\"vcp\":245,\"status\":\"ok\",\"current\":%u,\"max\":3,\"classification\":\"enum-advertised\",\"samples\":[{\"status\":\"ok\",\"current\":%u,\"max\":3},{\"status\":\"ok\",\"current\":%u,\"max\":3}]}", mode, mode, mode] : @"";
    NSString *unstable_read = unstable ? @",{\"vcp\":246,\"status\":\"ok\",\"current\":1,\"max\":3,\"classification\":\"unstable\",\"samples\":[{\"status\":\"ok\",\"current\":1,\"max\":3},{\"status\":\"ok\",\"current\":2,\"max\":3}]}" : @"";
    NSString *comma = include_mode ? @"," : @"";
    return [NSString stringWithFormat:
        @"{\"schemaVersion\":2,\"label\":\"%@\",\"display\":{\"productName\":\"LG\",\"manufacturer\":\"LG\",\"serial\":\"s\",\"provider\":\"%@\",\"branch\":\"b\",\"transport\":\"t\"},\"reads\":[%@%@{\"vcp\":16,\"status\":\"ok\",\"current\":%u,\"max\":%u,\"classification\":\"numeric\",\"samples\":[{\"status\":\"ok\",\"current\":%u,\"max\":%u},{\"status\":\"ok\",\"current\":%u,\"max\":%u}]}%@]}",
        label, provider, mode_read, comma, brightness, maximum, brightness, maximum, brightness, maximum, unstable_read];
}

static void write_fixture(NSString *path, NSString *contents) {
    NSError *error = nil;
    assert([contents writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:&error]);
}

static NSString *read_file(FILE *file) {
    char bytes[8192] = {};
    rewind(file);
    assert(fread(bytes, 1, sizeof(bytes) - 1, file) != 0);
    return [NSString stringWithUTF8String:bytes];
}

int main(void) {
    @autoreleasepool {
        NSString *directory = [NSTemporaryDirectory() stringByAppendingPathComponent:[[NSUUID UUID] UUIDString]];
        assert([[NSFileManager defaultManager] createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil]);
        NSString *fps = [directory stringByAppendingPathComponent:@"fps.json"];
        NSString *custom = [directory stringByAppendingPathComponent:@"custom.json"];
        NSString *vivid = [directory stringByAppendingPathComponent:@"vivid.json"];
        write_fixture(fps, report(@"FPS", 0, 70, 100, @"DCPDP13Service", YES, YES));
        write_fixture(custom, report(@"Custom", 1, 65, 90, @"DCPDP13Service", YES, NO));
        write_fixture(vivid, report(@"Vivid", 2, 80, 90, @"DCPDP13Service", YES, NO));
        const char *pair[] = {fps.fileSystemRepresentation, custom.fileSystemRepresentation};
        FILE *human = tmpfile(); FILE *errors = tmpfile();
        assert(human != NULL && errors != NULL);
        assert(rss_ddc_research_compare_files(2, pair, NULL, human, errors) == 0);
        NSString *pair_text = read_file(human);
        assert([pair_text containsString:@"Changed readable controls"] && [pair_text containsString:@"0x10"] &&
               [pair_text containsString:@"value-changed"] && [pair_text containsString:@"max-changed"] &&
               [pair_text containsString:@"read-status-changed"]);
        fclose(human); fclose(errors);

        const char *many[] = {fps.fileSystemRepresentation, custom.fileSystemRepresentation, vivid.fileSystemRepresentation};
        NSString *output = [directory stringByAppendingPathComponent:@"comparison.json"];
        human = tmpfile(); errors = tmpfile();
        assert(rss_ddc_research_compare_files(3, many, output.fileSystemRepresentation, human, errors) == 0);
        NSString *many_text = read_file(human);
        assert([many_text containsString:@"State matrix"] && [many_text containsString:@"strong-correlator"] &&
               [many_text containsString:@"incidental-change"]);
        NSData *output_data = [NSData dataWithContentsOfFile:output];
        NSDictionary *output_json = [NSJSONSerialization JSONObjectWithData:output_data options:0 error:nil];
        assert([output_json[@"schemaVersion"] isEqual:@1] && [output_json[@"identityMatch"] boolValue] && [output_json[@"rows"] count] >= 2);
        fclose(human); fclose(errors);

        NSString *legacy = [directory stringByAppendingPathComponent:@"legacy.json"];
        write_fixture(legacy, [report(@"", 0, 70, 100, @"other-provider", YES, NO) stringByReplacingOccurrencesOfString:@"\"schemaVersion\":2,\"label\":\"\"," withString:@"\"schemaVersion\":1,"]);
        const char *mismatch[] = {fps.fileSystemRepresentation, legacy.fileSystemRepresentation};
        human = tmpfile(); errors = tmpfile();
        assert(rss_ddc_research_compare_files(2, mismatch, NULL, human, errors) == 0);
        NSString *mismatch_text = read_file(human);
        assert([mismatch_text containsString:@"IDENTITY WARNING"] && [mismatch_text containsString:@"using filename label legacy"]);
        fclose(human); fclose(errors);
        [[NSFileManager defaultManager] removeItemAtPath:directory error:nil];
    }
    puts("test_research_compare: passed");
    return 0;
}
