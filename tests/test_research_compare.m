@import Foundation;

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "compare.h"

static NSString *report(NSString *label, unsigned int mode, unsigned int brightness, unsigned int maximum,
                        NSString *provider, BOOL include_mode, BOOL unstable, BOOL failure) {
    NSString *mode_read = include_mode ? [NSString stringWithFormat:
        @"{\"vcp\":245,\"status\":\"success\",\"current\":%u,\"max\":3,\"advertisedValues\":[0,1,2],\"classification\":\"enum-advertised\",\"samples\":[{\"status\":\"success\",\"current\":%u,\"max\":3},{\"status\":\"success\",\"current\":%u,\"max\":3},{\"status\":\"success\",\"current\":%u,\"max\":3}]}", mode, mode, mode, mode] : @"";
    NSString *unstable_read = unstable ? @",{\"vcp\":246,\"status\":\"success\",\"current\":1,\"max\":3,\"classification\":\"unstable\",\"samples\":[{\"status\":\"success\",\"current\":1,\"max\":3},{\"status\":\"success\",\"current\":2,\"max\":3},{\"status\":\"success\",\"current\":1,\"max\":3}]}" : @"";
    NSString *failure_read = failure ? @",{\"vcp\":250,\"status\":\"DDC/CI read failed\",\"current\":0,\"max\":0,\"classification\":\"transport-error\",\"samples\":[{\"status\":\"DDC/CI read failed\",\"current\":0,\"max\":0}]}" : @"";
    NSString *comma = include_mode ? @"," : @"";
    return [NSString stringWithFormat:
        @"{\"schemaVersion\":2,\"label\":\"%@\",\"display\":{\"productName\":\"LG\",\"manufacturer\":\"LG\",\"serial\":\"s\",\"provider\":\"%@\",\"branch\":\"b\",\"transport\":\"t\"},\"reads\":[%@%@{\"vcp\":16,\"status\":\"success\",\"current\":%u,\"max\":%u,\"classification\":\"numeric\",\"samples\":[{\"status\":\"success\",\"current\":%u,\"max\":%u},{\"status\":\"success\",\"current\":%u,\"max\":%u},{\"status\":\"success\",\"current\":%u,\"max\":%u}]}%@%@]}",
        label, provider, mode_read, comma, brightness, maximum, brightness, maximum, brightness, maximum, brightness, maximum, unstable_read, failure_read];
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
        NSString *hdr = [directory stringByAppendingPathComponent:@"hdr.json"];
        NSString *cinema = [directory stringByAppendingPathComponent:@"cinema.json"];
        NSString *rts = [directory stringByAppendingPathComponent:@"rts.json"];
        NSString *weakness = [directory stringByAppendingPathComponent:@"weakness.json"];
        NSString *reader = [directory stringByAppendingPathComponent:@"reader.json"];
        write_fixture(fps, report(@"FPS", 0, 70, 100, @"DCPDP13Service", YES, YES, YES));
        write_fixture(custom, report(@"Custom", 1, 65, 90, @"DCPDP13Service", YES, NO, NO));
        write_fixture(vivid, report(@"Vivid", 2, 80, 90, @"DCPDP13Service", YES, NO, NO));
        write_fixture(hdr, report(@"HDR-Effect", 3, 75, 90, @"DCPDP13Service", YES, NO, NO));
        write_fixture(cinema, report(@"Cinema", 0, 60, 90, @"DCPDP13Service", YES, NO, NO));
        write_fixture(rts, report(@"RTS", 1, 68, 90, @"DCPDP13Service", YES, NO, NO));
        write_fixture(weakness, report(@"Color-Weakness", 2, 72, 90, @"DCPDP13Service", YES, NO, NO));
        write_fixture(reader, report(@"Reader", 3, 55, 90, @"DCPDP13Service", YES, NO, NO));
        const char *pair[] = {fps.fileSystemRepresentation, custom.fileSystemRepresentation};
        FILE *human = tmpfile(); FILE *errors = tmpfile();
        assert(human != NULL && errors != NULL);
        assert(rss_ddc_research_compare_files(2, pair, NULL, human, errors) == 0);
        NSString *pair_text = read_file(human);
        assert([pair_text containsString:@"Changed readable controls"] && [pair_text containsString:@"0x10"] && [pair_text containsString:@"0x0046"] &&
               [pair_text containsString:@"value-changed"] && [pair_text containsString:@"max-changed"] &&
               [pair_text containsString:@"read-status-changed"] && ![pair_text containsString:@"0xF5  read-error"]);
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
        assert([output_json[@"schemaVersion"] isEqual:@1] && [output_json[@"identityMatch"] boolValue] && [output_json[@"rows"] count] >= 3);
        NSDictionary *enum_row = [output_json[@"rows"] filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"vcp == 245"]].firstObject;
        NSDictionary *numeric_row = [output_json[@"rows"] filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"vcp == 16"]].firstObject;
        NSDictionary *failure_row = [output_json[@"rows"] filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"vcp == 250"]].firstObject;
        assert([enum_row[@"observations"][0][@"state"] isEqualToString:@"readable"] &&
               [enum_row[@"observations"][0][@"current"] isEqual:@0] && [enum_row[@"observations"][0][@"advertisedValues"] count] == 3 &&
               [numeric_row[@"observations"][0][@"current"] isEqual:@70] &&
               [failure_row[@"observations"][0][@"state"] isEqualToString:@"read-error"]);
        fclose(human); fclose(errors);

        const char *eight[] = {fps.fileSystemRepresentation, custom.fileSystemRepresentation, vivid.fileSystemRepresentation,
            hdr.fileSystemRepresentation, cinema.fileSystemRepresentation, rts.fileSystemRepresentation,
            weakness.fileSystemRepresentation, reader.fileSystemRepresentation};
        NSString *eight_output = [directory stringByAppendingPathComponent:@"eight-comparison.json"];
        human = tmpfile(); errors = tmpfile();
        assert(rss_ddc_research_compare_files(8, eight, eight_output.fileSystemRepresentation, human, errors) == 0);
        NSString *eight_text = read_file(human);
        NSData *eight_data = [NSData dataWithContentsOfFile:eight_output];
        NSDictionary *eight_json = [NSJSONSerialization JSONObjectWithData:eight_data options:0 error:nil];
        NSDictionary *eight_enum = [eight_json[@"rows"] filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"vcp == 245"]].firstObject;
        assert([eight_text containsString:@"State matrix"] && [eight_text containsString:@"strong-correlator"] &&
               [eight_enum[@"observations"] count] == 8 && [eight_enum[@"observations"][7][@"current"] isEqual:@3] &&
               ![eight_text containsString:@"0xF5  read-error"]);
        fclose(human); fclose(errors);

        NSString *legacy = [directory stringByAppendingPathComponent:@"legacy.json"];
        write_fixture(legacy, [report(@"", 0, 70, 100, @"other-provider", YES, NO, NO) stringByReplacingOccurrencesOfString:@"\"schemaVersion\":2,\"label\":\"\"," withString:@"\"schemaVersion\":1,"]);
        const char *mismatch[] = {fps.fileSystemRepresentation, legacy.fileSystemRepresentation};
        human = tmpfile(); errors = tmpfile();
        assert(rss_ddc_research_compare_files(2, mismatch, NULL, human, errors) == 0);
        NSString *mismatch_text = read_file(human);
        assert([mismatch_text containsString:@"IDENTITY WARNING"] && [mismatch_text containsString:@"using filename label legacy"]);
        fclose(human); fclose(errors);

        NSString *invalid = [directory stringByAppendingPathComponent:@"invalid-success.json"];
        write_fixture(invalid, @"{\"schemaVersion\":2,\"label\":\"Invalid\",\"display\":{\"productName\":\"LG\",\"manufacturer\":\"LG\",\"serial\":\"s\",\"provider\":\"DCPDP13Service\",\"branch\":\"b\",\"transport\":\"t\"},\"reads\":[{\"vcp\":16,\"status\":\"success\",\"current\":100,\"max\":100,\"classification\":\"numeric\",\"samples\":[]}]}");
        const char *invalid_pair[] = {fps.fileSystemRepresentation, invalid.fileSystemRepresentation};
        human = tmpfile(); errors = tmpfile();
        assert(rss_ddc_research_compare_files(2, invalid_pair, NULL, human, errors) != 0);
        assert([read_file(errors) containsString:@"successful read has invalid classification or samples"]);
        fclose(human); fclose(errors);
        [[NSFileManager defaultManager] removeItemAtPath:directory error:nil];
    }
    puts("test_research_compare: passed");
    return 0;
}
