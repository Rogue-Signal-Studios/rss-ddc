@import Foundation;

#include <assert.h>
#include <stdio.h>

typedef struct {
    int status;
    NSString *stdout_text;
    NSString *stderr_text;
} CLIResult;

static CLIResult run_cli(NSArray<NSString *> *arguments) {
    NSString *path = [[[NSFileManager defaultManager] currentDirectoryPath] stringByAppendingPathComponent:@"rss-ddc-research"];
    assert([[NSFileManager defaultManager] isExecutableFileAtPath:path]);
    NSTask *task = [[NSTask alloc] init];
    NSPipe *stdout_pipe = [NSPipe pipe];
    NSPipe *stderr_pipe = [NSPipe pipe];
    task.launchPath = path;
    task.arguments = arguments;
    task.standardOutput = stdout_pipe;
    task.standardError = stderr_pipe;
    [task launch];
    [task waitUntilExit];
    NSData *stdout_data = [[stdout_pipe fileHandleForReading] readDataToEndOfFile];
    NSData *stderr_data = [[stderr_pipe fileHandleForReading] readDataToEndOfFile];
    return (CLIResult){.status = task.terminationStatus,
        .stdout_text = [[NSString alloc] initWithData:stdout_data encoding:NSUTF8StringEncoding],
        .stderr_text = [[NSString alloc] initWithData:stderr_data encoding:NSUTF8StringEncoding]};
}

int main(void) {
    @autoreleasepool {
        CLIResult result = run_cli(@[@"--help"]);
        assert(result.status == 0 && [result.stdout_text containsString:@"discover [options]"]);
        result = run_cli(@[@"discover", @"--help"]);
        assert(result.status == 0 && [result.stdout_text containsString:@"--no-restore"] && [result.stdout_text containsString:@"Safety:"]);
        result = run_cli(@[@"compare", @"--help"]);
        assert(result.status == 0 && [result.stdout_text containsString:@"offline-only"]);
        result = run_cli(@[@"discover", @"--unknown"]);
        assert(result.status != 0 && [result.stderr_text containsString:@"unknown option"] && [result.stderr_text containsString:@"discover --help"]);
        result = run_cli(@[@"discover", @"--display"]);
        assert(result.status != 0 && [result.stderr_text containsString:@"requires an argument"]);
        result = run_cli(@[@"discover", @"--display", @"0", @"--vcp", @"not-a-number"]);
        assert(result.status != 0 && [result.stderr_text containsString:@"invalid --display"]);
        result = run_cli(@[@"discover", @"--vcp", @"not-a-number"]);
        assert(result.status != 0 && [result.stderr_text containsString:@"invalid --vcp list"]);
        result = run_cli(@[@"discover", @"--display", @"2", @"--allow-set", @"--vcp", @"0x15", @"--values", @"0x31", @"--restore", @"--no-restore"]);
        assert(result.status != 0 && [result.stderr_text containsString:@"mutually exclusive"]);
        result = run_cli(@[@"discover", @"--display", @"2", @"--allow-set", @"--vcp", @"0x01", @"--values", @"0x31", @"--no-restore"]);
        assert(result.status != 0 && [result.stderr_text containsString:@"safety gate"]);
        result = run_cli(@[@"discover", @"--display", @"2", @"--allow-set", @"--vcp", @"0x15,0x16", @"--values", @"0x31", @"--no-restore"]);
        assert(result.status != 0 && [result.stderr_text containsString:@"safety gate"]);
        result = run_cli(@[@"compare", @"/definitely/not/a/report.json", @"/also/not/a/report.json"]);
        assert(result.status != 0 && [result.stderr_text containsString:@"cannot load"]);
    }
    puts("test_research_cli: passed");
    return 0;
}
