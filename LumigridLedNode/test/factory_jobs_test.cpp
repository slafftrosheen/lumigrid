#define main output_driver_regression_main
#include "output_drivers_test.cpp"
#undef main
#include "FactoryEffects.hpp"
#include "ControlSourceManager.hpp"
#include "CommandReceipt.hpp"
#include "JobManager.hpp"
#include "RuntimeMemory.hpp"
#define private public
#include "FactoryRunner.hpp"
#undef private

namespace lg {
TaskStackRegistry& task_stack_registry() { static TaskStackRegistry registry; return registry; }
}

namespace {
struct FactoryFixture {
    ControllerFixture output;
    ControlSourceManager sources;
    JobManager jobs;
    CommandJournal journal;
    FactoryRunner runner{sources, output.controller};
    FactoryFixture() {
        output.start(config(0, 1, 4));
        runner.set_command_journal(&journal);
        require(sources.init() == ESP_OK && runner.start() == ESP_OK, "factory fixture failed to start");
    }
    void consume() {
        FactoryCommand command;
        require(xQueueReceive(runner.queue_, &command, 0) == pdTRUE, "expected queued command");
        runner.apply_command(command, host_now_us.load() / 1000);
    }
    uint32_t start_burn_in() {
        uint32_t id = 0;
        require(runner.start_burn_in_job(&jobs, 1, 64, &id) && id != 0, "burn-in was rejected");
        consume();
        output.controller.pwm().set(0, 1000);
        require(output.controller.try_commit() == ESP_OK, "burn-in output could not commit");
        return id;
    }
    void fill_queue() {
        FactoryCommand command;
        command.type = FactoryCommandType::SelectEffect;
        for (unsigned i = 0; i < 8; ++i) require(runner.enqueue(command), "could not fill factory queue");
    }
};

void factory_case(const std::string& name) {
    if (name == "cancel_waits_for_blackout") {
        FactoryFixture f;
        const uint32_t id = f.start_burn_in();
        require(f.runner.stop_burn_in_job(), "cancellation was not queued");
        OperationJob job{};
        require(f.jobs.get_job(id, job) && job.state != JobState::Cancelled,
                "job was declared cancelled before the worker stopped its physical output");
        require(f.runner.status().session.state == FactoryTestState::Running,
                "session was declared aborted before the worker acknowledged it");
        require(f.runner.burn_in_active_, "physical worker ownership was released before cancellation acknowledgement");
        uint32_t replacement = 0;
        require(!f.runner.start_burn_in_job(&f.jobs, 1, 64, &replacement), "new burn-in overlapped cancellation of old output");
        f.output.pca.frame_hook = [&] {
            require(f.jobs.get_job(id, job) && job.state != JobState::Cancelled && f.runner.burn_in_active_,
                    "job slot was released before the hardware blackout transaction returned");
        };
        f.consume();
        require(f.jobs.get_job(id, job) && job.state == JobState::Cancelled, "worker did not acknowledge cancellation");
        require(!f.runner.burn_in_active_ && f.runner.status().session.state == FactoryTestState::Aborted,
                "worker retained old ownership after blackout");
        require_disabled("cancelled worker left physical PWM enabled");
        require(f.runner.start_burn_in_job(&f.jobs, 1, 64, &replacement), "new job rejected after cancellation completed");
        require(replacement != id, "replacement reused cancelled job identity");
        f.consume();
        FactoryCommand command;
        command.type = FactoryCommandType::Abort;
        command.command_id = f.journal.accept(CommandDomain::Factory);
        require(f.runner.enqueue(command), "journaled abort was rejected");
        CommandReceipt receipt{};
        require(f.journal.get(command.command_id, receipt) && receipt.state == CommandState::Accepted,
                "queued abort was marked applied before worker acknowledgement");
        f.consume();
        require(f.journal.get(command.command_id, receipt) && receipt.state == CommandState::Applied,
                "successful worker blackout did not apply the abort receipt");
    } else if (name == "cancel_queue_failure_retains_owner") {
        FactoryFixture f;
        const uint32_t id = f.start_burn_in();
        f.fill_queue();
        require(!f.runner.stop_burn_in_job(), "cancellation reported success although the command queue was full");
        OperationJob job{};
        require(f.jobs.get_job(id, job) && job.state != JobState::Cancelled && f.runner.burn_in_active_,
                "failed cancellation discarded the still-running job");
        require(f.runner.status().session.state == FactoryTestState::Running, "failed cancellation changed the session state");
    } else if (name == "start_queue_failure_finishes_job") {
        FactoryFixture f;
        f.fill_queue();
        uint32_t id = 0;
        require(!f.runner.start_burn_in_job(&f.jobs, 1, 64, &id), "full queue unexpectedly admitted burn-in");
        OperationJob job{};
        require(f.jobs.get_current(job) && job.state == JobState::Failed && !f.runner.burn_in_active_,
                "rejected burn-in left an active orphan job blocking all future operations");
        f.consume();
        require(f.runner.start_burn_in_job(&f.jobs, 1, 64, &id), "burn-in could not retry after enqueue failure");
    } else if (name == "stop_repeated_restart") {
        FactoryFixture f;
        f.runner.stop();
        f.runner.stop();
        require(f.runner.lifecycle_state() == FactoryRunner::LifecycleState::Stopped,
                "second stop changed a stopped runner back to Stopping");
        require(f.runner.start() == ESP_OK, "cleanly stopped runner could not restart");
    } else if (name == "cancel_blackout_failure_is_failed") {
        FactoryFixture f;
        const uint32_t id = f.start_burn_in();
        FactoryCommand command;
        command.type = FactoryCommandType::Abort;
        command.command_id = f.journal.accept(CommandDomain::Factory);
        require(f.runner.enqueue(command), "cancellation was not queued");
        f.output.pca.fail_frames = 8;
        f.consume();
        OperationJob job{};
        require(f.jobs.get_job(id, job) && job.state == JobState::Failed && job.error_code == ESP_ERR_TIMEOUT,
                "failed hardware blackout was reported as a successful cancellation");
        CommandReceipt receipt{};
        require(f.journal.get(command.command_id, receipt) && receipt.state == CommandState::Failed &&
                receipt.error_code == ESP_ERR_TIMEOUT, "failed blackout was reported as an applied abort command");
        require_disabled("failed cancellation did not retain the physical safety gate");
    } else if (name == "abort_before_pending_job_keeps_job") {
        FactoryFixture f;
        require(f.runner.abort(), "initial abort did not queue");
        uint32_t id = 0;
        require(f.runner.start_burn_in_job(&f.jobs, 1, 64, &id), "later start did not queue");
        f.consume();
        OperationJob job{};
        require(f.jobs.get_job(id, job) && job.state == JobState::Queued && f.runner.burn_in_active_,
                "abort of an earlier empty session cancelled a later queued job");
        f.consume();
        require(f.runner.status().session.state == FactoryTestState::Running, "later queued session did not start");
    } else if (name == "stale_abort_does_not_cancel_new_session") {
        FactoryFixture f;
        f.start_burn_in();
        const uint32_t old_session = f.runner.session().session_id;
        require(f.runner.stop_burn_in_job(), "first cancellation did not queue");
        f.consume();
        const uint32_t current_job = f.start_burn_in();
        require(f.runner.abort_test_session(old_session), "stale cancellation did not queue");
        f.consume();
        OperationJob job{};
        require(f.jobs.get_job(current_job, job) && job.state != JobState::Cancelled && f.runner.burn_in_active_,
                "old session cancellation released a new job's ownership");
        require(f.runner.session().state == FactoryTestState::Running && Pca9685Driver::is_outputs_enabled(),
                "old session cancellation stopped a newer physical test");
        FactoryCommand command;
        command.type = FactoryCommandType::Abort;
        command.session_id = old_session;
        command.command_id = f.journal.accept(CommandDomain::Factory);
        require(f.runner.enqueue(command), "journaled stale abort was rejected before worker inspection");
        f.consume();
        CommandReceipt receipt{};
        require(f.journal.get(command.command_id, receipt) && receipt.state == CommandState::Failed &&
                receipt.error_code == ESP_ERR_INVALID_STATE && f.runner.pending_command_id_ == 0,
                "stale abort left a pending or applied command receipt");
    } else if (name == "stop_waiting_api_callers") {
        FactoryFixture f;
        const auto status_mutex = f.runner.status_mutex_;
        const auto queue = f.runner.queue_;
        std::mutex gate;
        std::condition_variable condition;
        bool entered = false, released = false;
        auto pause_at_status_mutex = [&](SemaphoreHandle_t sem) {
            require(sem == status_mutex, "API caller paused at the wrong semaphore");
            std::unique_lock<std::mutex> lock(gate);
            entered = true; condition.notify_all();
            condition.wait(lock, [&] { return released; });
        };
        auto wait_for_caller = [&] {
            std::unique_lock<std::mutex> lock(gate);
            require(condition.wait_for(lock, std::chrono::seconds(2), [&] { return entered; }), "API caller did not enter");
        };
        auto release_caller = [&] {
            std::lock_guard<std::mutex> lock(gate);
            released = true; condition.notify_all();
        };
        auto reader = std::async(std::launch::async, [&] {
            host_before_take = pause_at_status_mutex;
            return f.runner.status();
        });
        wait_for_caller();
        f.runner.stop();
        require(f.runner.start() == ESP_OK, "runner could not restart with admitted API reader");
        release_caller();
        reader.get();
        require(host_unsafe_deletes == 0 && f.runner.status_mutex_ == status_mutex && f.runner.queue_ == queue,
                "stop deleted/replaced synchronization resources under an admitted recovery API caller");

        entered = released = false;
        auto writer = std::async(std::launch::async, [&] {
            host_before_take = pause_at_status_mutex;
            FactoryCommand command; command.type = FactoryCommandType::SelectEffect;
            return f.runner.enqueue(command);
        });
        wait_for_caller();
        f.runner.stop();
        require(f.runner.start() == ESP_OK, "runner could not restart with queued API writer");
        release_caller();
        require(!writer.get(), "old API command crossed into the restarted factory worker");
        FactoryCommand command;
        require(xQueueReceive(f.runner.queue_, &command, 0) == pdFALSE, "stale factory command survived restart");
    } else require(false, "unknown factory job case");
}
}

int main(int argc, char** argv) {
    require(argc == 2, "one factory job case is required");
    try { factory_case(argv[1]); std::cout << "PASS " << argv[1] << '\n'; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL " << argv[1] << ": " << error.what() << '\n'; return 1; }
}
