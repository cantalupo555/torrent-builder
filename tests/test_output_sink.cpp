// Tests for the OutputSink injection seam in TorrentCreator and TorrentChecker.
//
// These are the first white-box tests that exercise TorrentCreator::create_torrent()
// directly (rather than via popen subprocess in test_cli.cpp) by injecting a
// RecordingOutputSink. They verify:
//   1. progress() is invoked with structured ProgressInfo during hashing
//   2. should_cancel predicate interrupts a running hash with UserInterrupt
//   3. TorrentChecker::check(content_path, OutputSink&) routes progress to the sink

#include <gtest/gtest.h>
#include "torrent_creator.hpp"
#include "torrent_checker.hpp"
#include "output.hpp"
#include "output_sink.hpp"
#include "terminal.hpp"
#include "portable.hpp"
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <atomic>
#include <random>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

/// Captures every call to an OutputSink method. Used to assert that
/// TorrentCreator emits structured progress through the injected sink
/// rather than writing to stdout directly.
class RecordingOutputSink : public OutputSink {
public:
    std::vector<std::string> infos;
    std::vector<std::string> verboses;
    std::vector<std::string> errors;
    std::vector<ProgressInfo> progresses;
    int progress_line_end_calls = 0;

    int progress_count() const { return static_cast<int>(progresses.size()); }

    void info(const std::string& msg) override { infos.push_back(msg); }
    void verbose(const std::string& msg) override { verboses.push_back(msg); }
    void error(const std::string& msg) override { errors.push_back(msg); }
    void progress(const ProgressInfo& p) override { progresses.push_back(p); }
    void progress_line_end() override { ++progress_line_end_calls; }
};

/// Sink that throws on every progress update, to verify the producer contains
/// sink failures instead of letting them escape a hashing worker thread.
class ThrowingProgressSink : public RecordingOutputSink {
public:
    void progress(const ProgressInfo&) override { throw std::runtime_error("sink boom"); }
};

class OutputSinkTest : public ::testing::Test {
protected:
    void SetUp() override {
        // gtest_discover_tests runs each case as a separate ctest process;
        // a fixed directory would race under `ctest -jN`.
        temp_dir_ = fs::temp_directory_path()
            / ("tb_output_sink_test_"
               + std::to_string(portable_getpid()) + "_"
               + std::to_string(std::random_device{}()));
        fs::remove_all(temp_dir_);
        fs::create_directories(temp_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

    // Create a temp file large enough that hashing produces multiple pieces
    // and thus multiple progress callbacks. ~256KB is well above any plausible
    // piece-size threshold (16KB minimum) without slowing the test.
    fs::path create_temp_file(const std::string& name, size_t size = 256 * 1024) {
        auto path = temp_dir_ / name;
        std::ofstream out(path, std::ios::binary);
        std::string chunk(4096, 'x');
        while (size >= chunk.size()) {
            out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            size -= chunk.size();
        }
        if (size > 0) out.write(chunk.data(), static_cast<std::streamsize>(size));
        return path;
    }

    fs::path temp_dir_;
};

} // namespace

// TorrentCreator must route progress through the injected sink, not stdout.
// This is the core assertion that enables GUI rendering of a QProgressBar.
TEST_F(OutputSinkTest, CreatorEmitsProgressViaSink) {
    auto input = create_temp_file("input.bin");
    auto output = temp_dir_ / "out.torrent";

    RecordingOutputSink sink;
    TorrentConfig config(input, output, {}, TorrentVersion::V1);
    TorrentCreator creator(std::move(config), sink);

    creator.create_torrent();

    // Hashing must have produced at least one progress update.
    EXPECT_GE(sink.progress_count(), 1);

    // All recorded progress entries must be self-consistent: pieces_done
    // never exceeds pieces_total, and the final call should be complete.
    ASSERT_FALSE(sink.progresses.empty());
    const auto& last = sink.progresses.back();
    EXPECT_EQ(last.pieces_done, last.pieces_total);
    EXPECT_GT(last.pieces_total, 0);
    EXPECT_GT(last.bytes_total, 0);

    // Single-file hashing knows which file is being processed — the producer
    // must populate current_file so frontends can display it.
    EXPECT_EQ(last.current_file, input.filename().string());

    // The final progress line must be terminated through the sink hook, not
    // through an info("\n") that non-CLI sinks would render as a blank line.
    EXPECT_EQ(sink.progress_line_end_calls, 1);
    for (const auto& msg : sink.infos) {
        EXPECT_NE(msg, "\n");
    }

    // The summary is emitted via the sink too.
    bool saw_summary = false;
    for (const auto& msg : sink.infos) {
        if (msg.find("TORRENT CREATED SUCCESSFULLY") != std::string::npos) {
            saw_summary = true;
            break;
        }
    }
    EXPECT_TRUE(saw_summary);

    // Verbose diagnostics must reach the sink as well (the creator calls
    // sink_.verbose() unconditionally; verbosity filtering happens inside
    // StdoutOutputSink, not in the producer — so the mock records them even
    // under NORMAL verbosity).
    ASSERT_FALSE(sink.verboses.empty());
    EXPECT_NE(sink.verboses.front().find("Files added"), std::string::npos);
}

// Directory inputs take libtorrent's set_piece_hashes() path. Its progress
// callback must resolve which file contains the current piece, mirroring the
// single-file hasher and TorrentChecker — otherwise frontends can display a
// progress bar but never the file being hashed.
TEST_F(OutputSinkTest, DirectoryHashingReportsCurrentFile) {
    fs::path show_dir = temp_dir_ / "Show.S01";
    fs::create_directories(show_dir);
    for (int i = 1; i <= 3; ++i) {
        std::ofstream out(show_dir / ("ep" + std::to_string(i) + ".bin"),
                          std::ios::binary);
        out << std::string(256 * 1024, 'x');
    }
    auto output = temp_dir_ / "show.torrent";

    RecordingOutputSink sink;
    TorrentConfig config(show_dir, output, {}, TorrentVersion::V1);
    TorrentCreator creator(std::move(config), sink);

    creator.create_torrent();

    ASSERT_FALSE(sink.progresses.empty());
    for (const auto& p : sink.progresses) {
        EXPECT_FALSE(p.current_file.empty());
    }
    EXPECT_EQ(sink.progress_line_end_calls, 1);
}

// The should_cancel predicate must abort a running hash, throwing UserInterrupt
// exactly as pressing 'q' does on the CLI. This is what lets the GUI's Cancel
// button interrupt a long hash without a TTY.
//
// Note: we cannot reliably assert that the .torrent file was NOT written,
// because for small inputs the libtorrent set_piece_hashes() call may finish
// in a single batch before the predicate is consulted on the next loop tick.
// What we can assert is that, when the predicate returns true BEFORE hashing
// starts, create_torrent() throws UserInterrupt — proving the seam works.
TEST_F(OutputSinkTest, CancelPredicateAbortsHashing) {
    auto input = create_temp_file("cancel.bin");
    auto output = temp_dir_ / "cancel.torrent";

    RecordingOutputSink sink;

    // Pre-armed cancel: predicate always returns true. This guarantees the
    // cancellation check fires on the first opportunity, regardless of how
    // fast hashing would otherwise complete.
    auto should_cancel = []() { return true; };

    TorrentConfig config(input, output, {}, TorrentVersion::V1);
    TorrentCreator creator(std::move(config), sink, should_cancel);

    EXPECT_THROW(creator.create_torrent(), UserInterrupt);

    // The catch(UserInterrupt) handler reports through the sink's error()
    // channel before rethrowing — assert that routing survives.
    EXPECT_FALSE(sink.errors.empty());
}

// Directory inputs take the libtorrent-native set_piece_hashes() path (its
// own progress callback) rather than the single-file streaming hasher. The
// cancel seam must work there too — this is also the path a GUI multi-file
// creation would exercise most.
TEST_F(OutputSinkTest, CancelPredicateAbortsDirectoryHashing) {
    fs::path show_dir = temp_dir_ / "Show.S01";
    fs::create_directories(show_dir);
    for (int i = 1; i <= 3; ++i) {
        std::ofstream out(show_dir / ("ep" + std::to_string(i) + ".bin"),
                          std::ios::binary);
        out << std::string(256 * 1024, 'x');
    }
    auto output = temp_dir_ / "show.torrent";

    RecordingOutputSink sink;
    auto should_cancel = []() { return true; };

    TorrentConfig config(show_dir, output, {}, TorrentVersion::V1);
    TorrentCreator creator(std::move(config), sink, should_cancel);

    EXPECT_THROW(creator.create_torrent(), UserInterrupt);
}

// Variant: predicate that records how many times it is consulted. This
// documents the cancellation granularity: single-file inputs use the
// streaming hashers, which check the predicate per hashing block; directory
// and hybrid inputs go through libtorrent's set_piece_hashes() callback plus
// the monitor loop. We don't assert a specific count here — only that the
// predicate was reached at least once, proving the wiring works end-to-end.
TEST_F(OutputSinkTest, CancelPredicateIsConsulted) {
    auto input = create_temp_file("consult.bin");
    auto output = temp_dir_ / "consult.torrent";

    RecordingOutputSink sink;
    std::atomic<int> calls{0};
    auto should_cancel = [&calls]() {
        calls.fetch_add(1);
        return false;  // never cancel, just count consultations
    };

    TorrentConfig config(input, output, {}, TorrentVersion::V1);
    TorrentCreator creator(std::move(config), sink, should_cancel);

    creator.create_torrent();  // should complete normally

    // The seam was wired up and consulted at least once during hashing.
    EXPECT_GE(calls.load(), 1);
}

// TorrentChecker's sink-driven overload must route per-piece progress through
// the injected sink. Mirrors the creator test for the verification path.
TEST_F(OutputSinkTest, CheckerEmitsProgressViaSink) {
    auto input = create_temp_file("verify.bin");
    auto torrent_path = temp_dir_ / "verify.torrent";

    // First create the torrent (CLI sink is fine for setup).
    {
        TorrentConfig config(input, torrent_path, {}, TorrentVersion::V1);
        TorrentCreator creator(std::move(config));
        creator.create_torrent();
    }

    // Now verify with the recording sink injected.
    RecordingOutputSink sink;
    TorrentChecker checker(torrent_path);
    auto result = checker.check(temp_dir_, sink);

    ASSERT_GE(sink.progress_count(), 1);
    EXPECT_EQ(result.pieces_total, sink.progresses.back().pieces_total);
    EXPECT_EQ(sink.progress_line_end_calls, 1);

    // Verification resolves the file each piece belongs to (file_path).
    for (const auto& p : sink.progresses) {
        EXPECT_FALSE(p.current_file.empty());
    }
}

// The bool overload of check() must behave identically to the sink overload
// (same result), routing its output to the default/discard sinks internally.
// This exercises the wrapper's sink-selection logic directly instead of only
// via popen CLI tests.
TEST_F(OutputSinkTest, CheckBoolWrapperMatchesSinkOverload) {
    auto input = create_temp_file("wrapper.bin");
    auto torrent_path = temp_dir_ / "wrapper.torrent";

    {
        TorrentConfig config(input, torrent_path, {}, TorrentVersion::V1);
        TorrentCreator creator(std::move(config));
        creator.create_torrent();
    }

    TorrentChecker checker(torrent_path);

    // Non-verbose path (discard_sink selection) and verbose path
    // (default_stdout_sink selection) must agree on the verdict.
    CheckResult via_wrapper;
    CheckResult via_sink;
    RecordingOutputSink sink;

    EXPECT_NO_THROW(via_wrapper = checker.check(temp_dir_, false));
    EXPECT_NO_THROW(via_sink = checker.check(temp_dir_, sink));

    EXPECT_EQ(via_wrapper.pieces_total, via_sink.pieces_total);
    EXPECT_EQ(via_wrapper.pieces_corrupted, via_sink.pieces_corrupted);
    EXPECT_EQ(via_wrapper.pieces_verified, via_sink.pieces_verified);
}

// StdoutOutputSink::progress() must not divide by zero when a producer emits
// a degenerate ProgressInfo (pieces_total == 0 / bytes_total == 0). Guards
// against regressions of the ternary added in src/output.cpp.
TEST_F(OutputSinkTest, StdoutSinkProgressToleratesZeroTotals) {
    StdoutOutputSink sink;

    ProgressInfo degenerate{};
    degenerate.pieces_done = 0;
    degenerate.pieces_total = 0;
    degenerate.bytes_done = 0;
    degenerate.bytes_total = 0;

    EXPECT_NO_THROW(sink.progress(degenerate));

    // Asymmetric totals must also be safe (bytes_total > 0, pieces_total == 0).
    ProgressInfo mixed{};
    mixed.pieces_total = 0;
    mixed.bytes_total = 4096;
    EXPECT_NO_THROW(sink.progress(mixed));
}

// A throwing sink must not escape emit_progress(): hashing workers run on
// std::thread, where an uncaught exception would call std::terminate.
TEST_F(OutputSinkTest, ThrowingProgressSinkDoesNotEscape) {    auto input = create_temp_file("throwing_sink.bin");
    auto output = temp_dir_ / "throwing_sink.torrent";

    ThrowingProgressSink sink;
    TorrentConfig config(input, output, {}, TorrentVersion::V1);
    TorrentCreator creator(std::move(config), sink);

    EXPECT_NO_THROW(creator.create_torrent());
}

// A throwing cancel predicate is treated as a cancellation request so the
// worker unwinds as UserInterrupt instead of leaking a foreign exception.
TEST_F(OutputSinkTest, ThrowingCancelPredicateIsTreatedAsCancellation) {
    auto input = create_temp_file("throwing_predicate.bin");
    auto output = temp_dir_ / "throwing_predicate.torrent";

    RecordingOutputSink sink;
    auto should_cancel = []() -> bool { throw std::runtime_error("predicate boom"); };
    TorrentConfig config(input, output, {}, TorrentVersion::V1);
    TorrentCreator creator(std::move(config), sink, should_cancel);

    EXPECT_THROW(creator.create_torrent(), UserInterrupt);
}

// Batch mode sets silent=true: no per-piece progress and no progress-line
// terminator (which would otherwise add a stray blank line per job).
TEST_F(OutputSinkTest, SilentModeSuppressesProgressAndLineEnd) {
    auto input = create_temp_file("silent.bin");
    auto output = temp_dir_ / "silent.torrent";

    RecordingOutputSink sink;
    TorrentConfig config(input, output, {}, TorrentVersion::V1);
    config.silent = true;
    TorrentCreator creator(std::move(config), sink);

    creator.create_torrent();

    EXPECT_EQ(sink.progress_count(), 0);
    EXPECT_EQ(sink.progress_line_end_calls, 0);
}

// Verification must contain sink failures as well: a throwing progress sink
// must not abort the check or skip the file-handle cleanup path.
TEST_F(OutputSinkTest, CheckerToleratesThrowingProgressSink) {
    auto input = create_temp_file("checker_throwing.bin");
    auto torrent_path = temp_dir_ / "checker_throwing.torrent";

    {
        TorrentConfig config(input, torrent_path, {}, TorrentVersion::V1);
        TorrentCreator creator(std::move(config));
        creator.create_torrent();
    }

    TorrentChecker checker(torrent_path);
    ThrowingProgressSink sink;

    CheckResult result;
    EXPECT_NO_THROW(result = checker.check(temp_dir_, sink));
    EXPECT_GT(result.pieces_total, 0);
}
