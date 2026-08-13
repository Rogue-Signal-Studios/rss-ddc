@import Foundation;

#include <stdio.h>

#include "compare.h"

static NSString *const RSSResearchStateStable = @"stable";
static NSString *const RSSResearchStateUnstable = @"unstable";
static NSString *const RSSResearchStateError = @"read-error";
static NSString *const RSSResearchStateMissing = @"missing";

static NSDictionary *dictionary(id value) { return [value isKindOfClass:[NSDictionary class]] ? value : nil; }
static NSArray *array(id value) { return [value isKindOfClass:[NSArray class]] ? value : nil; }
static NSString *string(id value) { return [value isKindOfClass:[NSString class]] ? value : nil; }
static NSNumber *number(id value) { return [value isKindOfClass:[NSNumber class]] ? value : nil; }

static NSString *fallback_label(NSString *path) {
    return [[path lastPathComponent] stringByDeletingPathExtension];
}

static bool stable_read(NSDictionary *read) {
    NSString *classification = string(read[@"classification"]);
    if (![string(read[@"status"]) isEqualToString:@"ok"] ||
        !([classification isEqualToString:@"numeric"] || [classification isEqualToString:@"enum-advertised"] ||
          [classification isEqualToString:@"readable-unknown"])) return false;
    NSNumber *current = number(read[@"current"]);
    NSNumber *maximum = number(read[@"max"]);
    NSArray *samples = array(read[@"samples"]);
    if (current == nil || maximum == nil || samples.count == 0) return false;
    for (id value in samples) {
        NSDictionary *sample = dictionary(value);
        if (sample == nil || ![string(sample[@"status"]) isEqualToString:@"ok"] ||
            ![number(sample[@"current"]) isEqual:current] || ![number(sample[@"max"]) isEqual:maximum]) return false;
    }
    return true;
}

static NSMutableDictionary *read_observation(NSDictionary *read) {
    NSMutableDictionary *observation = [NSMutableDictionary dictionary];
    if (stable_read(read)) {
        observation[@"state"] = RSSResearchStateStable;
        observation[@"current"] = number(read[@"current"]);
        observation[@"max"] = number(read[@"max"]);
        observation[@"classification"] = string(read[@"classification"]);
    } else {
        NSString *classification = string(read[@"classification"]);
        observation[@"state"] = [classification isEqualToString:@"unstable"] ? RSSResearchStateUnstable : RSSResearchStateError;
        observation[@"status"] = string(read[@"status"]) ?: @"missing-status";
    }
    return observation;
}

static NSDictionary *load_report(NSString *path, NSMutableArray *warnings, NSError **out_error) {
    NSData *data = [NSData dataWithContentsOfFile:path options:0 error:out_error];
    if (data == nil) return nil;
    id decoded = [NSJSONSerialization JSONObjectWithData:data options:0 error:out_error];
    NSDictionary *root = dictionary(decoded);
    NSNumber *version = number(root[@"schemaVersion"]);
    NSDictionary *display = dictionary(root[@"display"]);
    NSArray *reads = array(root[@"reads"]);
    if (root == nil || version == nil || (version.integerValue != 1 && version.integerValue != 2) || display == nil || reads == nil) {
        if (out_error != NULL) *out_error = [NSError errorWithDomain:@"rss-ddc-research" code:1 userInfo:@{NSLocalizedDescriptionKey: @"not a supported discovery report"}];
        return nil;
    }
    NSMutableDictionary *observations = [NSMutableDictionary dictionary];
    for (id item in reads) {
        NSDictionary *read = dictionary(item);
        NSNumber *vcp = number(read[@"vcp"]);
        if (read == nil || vcp == nil || vcp.integerValue < 0 || vcp.integerValue > 255 || observations[vcp] != nil) continue;
        observations[vcp] = read_observation(read);
    }
    NSString *label = string(root[@"label"]);
    if (label.length == 0) {
        label = fallback_label(path);
        [warnings addObject:[NSString stringWithFormat:@"%@ is schema v1 or unlabeled; using filename label %@", path, label]];
    }
    return @{@"path": path, @"label": label, @"display": display, @"observations": observations};
}

static NSString *identity(NSDictionary *report) {
    NSDictionary *display = report[@"display"];
    return [NSString stringWithFormat:@"%@|%@|%@|%@|%@|%@", string(display[@"productName"]) ?: @"", string(display[@"manufacturer"]) ?: @"",
            string(display[@"serial"]) ?: @"", string(display[@"provider"]) ?: @"", string(display[@"branch"]) ?: @"", string(display[@"transport"]) ?: @""];
}

static NSString *rank_row(NSArray *observations) {
    NSUInteger stable = 0, enum_count = 0;
    NSMutableSet *values = [NSMutableSet set];
    for (NSDictionary *observation in observations) {
        if (![observation[@"state"] isEqualToString:RSSResearchStateStable]) continue;
        ++stable;
        [values addObject:observation[@"current"]];
        if ([observation[@"classification"] isEqualToString:@"enum-advertised"]) ++enum_count;
    }
    if (stable != observations.count || values.count < 2) return @"unchanged-or-insufficient-evidence";
    if (enum_count == observations.count && values.count >= 3) return @"strong-correlator";
    if (enum_count != 0) return @"possible-correlator";
    return @"incidental-change";
}

static NSInteger rank_order(NSString *rank) {
    if ([rank isEqualToString:@"strong-correlator"]) return 0;
    if ([rank isEqualToString:@"possible-correlator"]) return 1;
    if ([rank isEqualToString:@"incidental-change"]) return 2;
    return 3;
}

int rss_ddc_research_compare_files(int path_count, const char *const *paths, const char *json_output_path,
                                   FILE *human_output, FILE *error_output) {
    if (path_count < 2 || paths == NULL || human_output == NULL || error_output == NULL) return 1;
    NSMutableArray *warnings = [NSMutableArray array];
    NSMutableArray *reports = [NSMutableArray array];
    for (int index = 0; index < path_count; ++index) {
        NSError *error = nil;
        NSDictionary *report = load_report([NSString stringWithUTF8String:paths[index]], warnings, &error);
        if (report == nil) { fprintf(error_output, "rss-ddc-research: cannot load %s: %s\n", paths[index], error.localizedDescription.UTF8String); return 1; }
        [reports addObject:report];
    }
    NSString *reference_identity = identity(reports[0]);
    bool identity_match = true;
    for (NSDictionary *report in reports) if (![identity(report) isEqualToString:reference_identity]) identity_match = false;
    if (!identity_match) [warnings addObject:@"reports do not identify the same display/provider/transport; correlations may be invalid"];

    NSMutableSet *vcp_set = [NSMutableSet set];
    for (NSDictionary *report in reports) [vcp_set addObjectsFromArray:[report[@"observations"] allKeys]];
    NSArray *vcps = [[vcp_set allObjects] sortedArrayUsingSelector:@selector(compare:)];
    NSMutableArray *rows = [NSMutableArray array];
    for (NSNumber *vcp in vcps) {
        NSMutableArray *observations = [NSMutableArray array];
        bool value_changed = false, max_changed = false, state_changed = false, missing_changed = false;
        NSNumber *first_value = nil, *first_max = nil;
        NSString *first_state = nil;
        for (NSDictionary *report in reports) {
            NSDictionary *observation = report[@"observations"][vcp];
            if (observation == nil) observation = @{@"state": RSSResearchStateMissing};
            [observations addObject:observation];
            NSString *state = observation[@"state"];
            if (first_state == nil) first_state = state; else if (![first_state isEqualToString:state]) state_changed = true;
            if ([state isEqualToString:RSSResearchStateMissing] != [first_state isEqualToString:RSSResearchStateMissing]) missing_changed = true;
            if ([state isEqualToString:RSSResearchStateStable]) {
                if (first_value == nil) { first_value = observation[@"current"]; first_max = observation[@"max"]; }
                else { if (![first_value isEqual:observation[@"current"]]) value_changed = true; if (![first_max isEqual:observation[@"max"]]) max_changed = true; }
            }
        }
        NSString *ranking = rank_row(observations);
        [rows addObject:@{@"vcp": vcp, @"observations": observations, @"valueChanged": @(value_changed), @"maxChanged": @(max_changed),
                          @"stateChanged": @(state_changed), @"missingChanged": @(missing_changed), @"ranking": ranking}];
    }
    NSArray *ranked = [rows sortedArrayUsingComparator:^NSComparisonResult(NSDictionary *left, NSDictionary *right) {
        NSInteger first = rank_order(left[@"ranking"]), second = rank_order(right[@"ranking"]);
        if (first != second) return first < second ? NSOrderedAscending : NSOrderedDescending;
        return [left[@"vcp"] compare:right[@"vcp"]];
    }];
    NSUInteger changed = 0, unchanged = 0, missing_or_unreadable = 0;
    for (NSDictionary *row in rows) {
        if ([row[@"valueChanged"] boolValue] || [row[@"maxChanged"] boolValue] || [row[@"stateChanged"] boolValue]) ++changed; else ++unchanged;
        for (NSDictionary *observation in row[@"observations"]) if (![observation[@"state"] isEqualToString:RSSResearchStateStable]) { ++missing_or_unreadable; break; }
    }
    fprintf(human_output, "Offline comparison (%lu report%s)%s\n", (unsigned long)reports.count, reports.count == 1 ? "" : "s", identity_match ? "" : " — IDENTITY WARNING");
    fprintf(human_output, "Labels:"); for (NSDictionary *report in reports) fprintf(human_output, " %s", [report[@"label"] UTF8String]); fprintf(human_output, "\n\n");
    if (reports.count == 2) fprintf(human_output, "Changed readable controls:\nVCP    %s    %s    Change\n", [reports[0][@"label"] UTF8String], [reports[1][@"label"] UTF8String]);
    else { fprintf(human_output, "State matrix (stable values only):\nVCP   "); for (NSDictionary *report in reports) fprintf(human_output, " %-10s", [report[@"label"] UTF8String]); fprintf(human_output, " Ranking\n"); }
    for (NSDictionary *row in ranked) {
        bool show = reports.count == 2 ? ([row[@"valueChanged"] boolValue] || [row[@"maxChanged"] boolValue] || [row[@"stateChanged"] boolValue]) : true;
        if (!show) continue;
        fprintf(human_output, "0x%02X", [row[@"vcp"] unsignedIntValue]);
        for (NSDictionary *observation in row[@"observations"]) {
            if ([observation[@"state"] isEqualToString:RSSResearchStateStable]) fprintf(human_output, reports.count == 2 ? "  0x%04X" : " %-10u", [observation[@"current"] unsignedIntValue]);
            else fprintf(human_output, reports.count == 2 ? "  %-6s" : " %-10s", [observation[@"state"] UTF8String]);
        }
        if (reports.count == 2) fprintf(human_output, "  %s%s%s%s\n", [row[@"valueChanged"] boolValue] ? "value-changed " : "", [row[@"maxChanged"] boolValue] ? "max-changed " : "", [row[@"stateChanged"] boolValue] ? "read-status-changed " : "", [row[@"missingChanged"] boolValue] ? "missing/newly-readable" : "");
        else fprintf(human_output, " %s\n", [row[@"ranking"] UTF8String]);
    }
    fprintf(human_output, "\nUnchanged controls: %lu\nChanged controls: %lu\nMissing/unreadable controls: %lu\n", (unsigned long)unchanged, (unsigned long)changed, (unsigned long)missing_or_unreadable);
    for (NSString *warning in warnings) fprintf(human_output, "Warning: %s\n", warning.UTF8String);
    if (json_output_path != NULL) {
        if ([[NSFileManager defaultManager] fileExistsAtPath:[NSString stringWithUTF8String:json_output_path]]) { fprintf(error_output, "rss-ddc-research: comparison report already exists: %s\n", json_output_path); return 1; }
        NSMutableArray *metadata = [NSMutableArray array];
        for (NSDictionary *report in reports) [metadata addObject:@{@"label": report[@"label"], @"path": report[@"path"]}];
        NSDictionary *output = @{@"schemaVersion": @1, @"reports": metadata, @"identityMatch": @(identity_match), @"rows": ranked, @"warnings": warnings};
        NSError *error = nil;
        NSData *data = [NSJSONSerialization dataWithJSONObject:output options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:&error];
        if (data == nil || ![data writeToFile:[NSString stringWithUTF8String:json_output_path] options:NSDataWritingWithoutOverwriting error:&error]) {
            fprintf(error_output, "rss-ddc-research: cannot write comparison report: %s\n", error.localizedDescription.UTF8String); return 1;
        }
    }
    return 0;
}
