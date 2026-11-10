/*
 * Copyright 2014-2025 Real Logic Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <thread>
#include <gtest/gtest.h>

#include "gmock/gmock-matchers.h"

extern "C"
{
#include "aeron_agent.h"
#include "util/aeron_error.h"
}

class AgentTest : public testing::Test
{
};

TEST_F(AgentTest, shouldFailStartIfRunnerIsNull)
{
    EXPECT_EQ(-1, aeron_agent_start(nullptr));
    EXPECT_EQ(EINVAL, aeron_errcode());
    EXPECT_THAT(aeron_errmsg(), testing::HasSubstr("runner is null"));
}

class StateTest : public testing::TestWithParam<uint8_t>
{
};

INSTANTIATE_TEST_SUITE_P(
    NonInitState,
    StateTest,
    testing::Values(
        AERON_AGENT_STATE_UNUSED,
        AERON_AGENT_STATE_STARTED,
        AERON_AGENT_STATE_MANUAL,
        AERON_AGENT_STATE_STOPPED));

TEST_P(StateTest, shouldFailStartIfRunnerIsNotInTheCorrectState)
{
    aeron_agent_runner_t runner;
    runner.state = GetParam();

    EXPECT_EQ(-1, aeron_agent_start(&runner));
    EXPECT_EQ(EINVAL, aeron_errcode());
    EXPECT_THAT(aeron_errmsg(), testing::HasSubstr("runner must be in AERON_AGENT_STATE_INITED(1)"));
}

TEST_F(AgentTest, shouldRejectInitIfRunnerIsNull)
{
    aeron_agent_on_start_func_t on_start = [](void *state, const char *role_name){};
    aeron_agent_do_work_func_t do_work = [](void *){ return 0; };
    aeron_agent_on_close_func_t on_close = [](void *){};

    EXPECT_EQ(-1, aeron_agent_init(
        nullptr,
        "test",
        this,
        on_start,
        this,
        do_work,
        on_close,
        aeron_idle_strategy_noop_idle,
        this));
    EXPECT_EQ(EINVAL, aeron_errcode());
    EXPECT_THAT(aeron_errmsg(), testing::HasSubstr("Parameters must not be null"));
}

TEST_F(AgentTest, shouldRejectInitIfRoleNameIsNull)
{
    aeron_agent_on_start_func_t on_start = [](void *state, const char *role_name){};
    aeron_agent_do_work_func_t do_work = [](void *){ return 0; };
    aeron_agent_on_close_func_t on_close = [](void *){};

    aeron_agent_runner_t runner;

    EXPECT_EQ(-1, aeron_agent_init(
        &runner,
        nullptr,
        this,
        on_start,
        this,
        do_work,
        on_close,
        aeron_idle_strategy_noop_idle,
        this));
    EXPECT_EQ(EINVAL, aeron_errcode());
    EXPECT_THAT(aeron_errmsg(), testing::HasSubstr("Parameters must not be null"));
}

TEST_F(AgentTest, shouldRejectInitIfDoWorkIsNull)
{
    aeron_agent_on_start_func_t on_start = [](void *state, const char *role_name){};
    aeron_agent_on_close_func_t on_close = [](void *){};

    aeron_agent_runner_t runner;

    EXPECT_EQ(-1, aeron_agent_init(
        &runner,
        "test2",
        this,
        on_start,
        this,
        nullptr,
        on_close,
        aeron_idle_strategy_noop_idle,
        this));
    EXPECT_EQ(EINVAL, aeron_errcode());
    EXPECT_THAT(aeron_errmsg(), testing::HasSubstr("Parameters must not be null"));
}

TEST_F(AgentTest, shouldRejectInitIfIdleStrategyIsNull)
{
    aeron_agent_on_start_func_t on_start = [](void *state, const char *role_name){};
    aeron_agent_do_work_func_t do_work = [](void *){ return 0; };
    aeron_agent_on_close_func_t on_close = [](void *){};

    aeron_agent_runner_t runner;

    EXPECT_EQ(-1, aeron_agent_init(
        &runner,
        "test2",
        this,
        on_start,
        this,
        do_work,
        on_close,
        nullptr,
        this));
    EXPECT_EQ(EINVAL, aeron_errcode());
    EXPECT_THAT(aeron_errmsg(), testing::HasSubstr("Parameters must not be null"));
}

TEST_F(AgentTest, shouldInitAgent)
{
    std::atomic<bool> on_start_called(false);
    aeron_agent_on_start_func_t on_start = [](void *state, const char *role_name){};
    aeron_agent_do_work_func_t do_work = [](void *){ return 0; };

    int idle_counter;

    aeron_agent_runner_t runner;

    EXPECT_EQ(0, aeron_agent_init(
        &runner,
        "test ok",
        this,
        on_start,
        &on_start_called,
        do_work,
        nullptr,
        aeron_idle_strategy_yielding_idle,
        &idle_counter));
    EXPECT_EQ(AERON_AGENT_STATE_INITED, runner.state);
    EXPECT_TRUE(runner.running);
    EXPECT_EQ(on_start, runner.on_start);
    EXPECT_EQ(&on_start_called, runner.on_start_state);
    EXPECT_EQ(do_work, runner.do_work);
    EXPECT_EQ(nullptr, runner.on_close);
    EXPECT_EQ(aeron_idle_strategy_yielding_idle, runner.idle_strategy);
    EXPECT_EQ(&idle_counter, runner.idle_strategy_state);
    EXPECT_EQ(this, runner.agent_state);
    EXPECT_STREQ("test ok", runner.role_name);

    EXPECT_EQ(0, aeron_agent_close(&runner));
}

TEST_F(AgentTest, shouldStartExecutingOnANewThread)
{
    std::atomic<size_t> on_start_count(0);
    aeron_agent_on_start_func_t on_start = [](void *state, const char *role_name)
    {
        auto cnt = reinterpret_cast<std::atomic<size_t> *>(state);
        cnt->fetch_add(1);
    };

    typedef struct state
    {
        std::atomic<size_t> do_work_count;
        std::atomic<size_t> on_close_count;
    } agent_state_t;

    agent_state_t agent_state;
    agent_state.do_work_count = 0;
    agent_state.on_close_count = 0;

    aeron_agent_do_work_func_t do_work = [](void *state)
    {
        auto as = reinterpret_cast<agent_state_t *>(state);
        as->do_work_count.fetch_add(1);
        return 1;
    };

    aeron_agent_on_close_func_t on_close = [](void *state)
    {
        auto as = reinterpret_cast<agent_state_t *>(state);
        as->on_close_count.fetch_add(1);
    };

    std::atomic<size_t> idle_counter(0);
    aeron_idle_strategy_func_t idle_strategy_func = [](void *state, int work_count)
    {
        auto cnt = reinterpret_cast<std::atomic<size_t> *>(state);
        cnt->fetch_add(work_count);
    };

    aeron_agent_runner_t runner;

    EXPECT_EQ(0, aeron_agent_init(
        &runner,
        "brrr",
        &agent_state,
        on_start,
        &on_start_count,
        do_work,
        on_close,
        idle_strategy_func,
        &idle_counter));

    EXPECT_EQ(0, aeron_agent_start(&runner));
    EXPECT_EQ(AERON_AGENT_STATE_STARTED, runner.state);
    EXPECT_TRUE(runner.running);

    while (0 == on_start_count.load() || agent_state.do_work_count.load() < 5)
    {
        std::this_thread::yield();
    }
    EXPECT_TRUE(runner.running);
    EXPECT_EQ(0, agent_state.on_close_count.load());

    EXPECT_EQ(0, aeron_agent_stop(&runner));
    EXPECT_EQ(AERON_AGENT_STATE_STOPPED, runner.state);
    EXPECT_FALSE(runner.running);
    EXPECT_EQ(0, agent_state.on_close_count.load());
    EXPECT_EQ(agent_state.do_work_count.load(), idle_counter.load());

    EXPECT_EQ(0, aeron_agent_close(&runner));
    EXPECT_EQ(1, agent_state.on_close_count.load());
}

TEST_F(AgentTest, stopShouldRejectNullRunner)
{
    EXPECT_EQ(-1, aeron_agent_stop(nullptr));
    EXPECT_EQ(EINVAL, aeron_errcode());
}

class StateTest2 : public testing::TestWithParam<uint8_t>
{
};

INSTANTIATE_TEST_SUITE_P(
    InvalidStopTests,
    StateTest2,
    testing::Values(
        AERON_AGENT_STATE_UNUSED,
        AERON_AGENT_STATE_INITED,
        AERON_AGENT_STATE_STOPPING,
        AERON_AGENT_STATE_STOPPED));

TEST_P(StateTest2, shouldMarkAsNotRunningWhenStoppingInUnexpectedStates)
{
    aeron_agent_runner_t runner;
    runner.running = true;
    runner.state = GetParam();

    aeron_err_clear();

    EXPECT_EQ(0, aeron_agent_stop(&runner));
    EXPECT_EQ(0, aeron_errcode());
    EXPECT_FALSE(runner.running);
    EXPECT_EQ(GetParam(), runner.state);
}

TEST_F(AgentTest, shouldStopAgentInManualState)
{
    aeron_agent_runner_t runner;
    runner.running = true;
    runner.state = AERON_AGENT_STATE_MANUAL;

    EXPECT_EQ(0, aeron_agent_stop(&runner));
    EXPECT_FALSE(runner.running);
    EXPECT_EQ(AERON_AGENT_STATE_STOPPED, runner.state);
}

TEST_F(AgentTest, shouldReturnIsRunning)
{
    aeron_agent_runner_t runner;
    runner.running = true;

    EXPECT_TRUE(aeron_agent_is_running(&runner));

    runner.running = false;
    EXPECT_FALSE(aeron_agent_is_running(&runner));
}

TEST_F(AgentTest, shouldRunIdleStrategyWhenCalledDirectly)
{
    typedef struct
    {
        size_t call_count;
        int total;
    } idle_data_t;

    idle_data_t idle_data;
    idle_data.total = 0;
    idle_data.call_count = 0;
    aeron_agent_runner_t runner;
    runner.idle_strategy_state = &idle_data;
    runner.idle_strategy = [](void *state, int work_count)
    {
        auto data = reinterpret_cast<idle_data_t *>(state);
        data->call_count++;
        data->total += work_count;
    };

    aeron_agent_idle(&runner, 5);
    aeron_agent_idle(&runner, 0);
    aeron_agent_idle(&runner, -3);

    EXPECT_EQ(3, idle_data.call_count);
    EXPECT_EQ(2, idle_data.total);
}

TEST_F(AgentTest, shouldCallDoWorkDirectly)
{
    int count = 0;
    aeron_agent_runner_t runner;
    runner.do_work = [](void *state)
    {
        auto cnt = reinterpret_cast<int *>(state);
        return (*cnt)++;
    };
    runner.agent_state = &count;

    EXPECT_EQ(0, aeron_agent_do_work(&runner));
    EXPECT_EQ(1, aeron_agent_do_work(&runner));

    EXPECT_EQ(2, count);
}

TEST_F(AgentTest, shouldExitSilentlyIfDoWorkReturnsNegativeValue)
{
    std::atomic<size_t> call_count(0);
    aeron_agent_do_work_func_t do_work = [](void *state)
    {
        auto cnt = reinterpret_cast<std::atomic<size_t> *>(state);
        if (cnt->fetch_add(1) > 0)
        {
            AERON_SET_ERR(EINPROGRESS, "%s", "error");
            return -1;
        }
        return 1;
    };

    std::atomic<size_t> idle_counter(0);
    aeron_idle_strategy_func_t idle_strategy_func = [](void *state, int work_count)
    {
        auto cnt = reinterpret_cast<std::atomic<size_t> *>(state);
        cnt->fetch_add(work_count);
    };

    aeron_agent_runner_t runner;

    EXPECT_EQ(0, aeron_agent_init(
        &runner,
        "second",
        &call_count,
        nullptr,
        nullptr,
        do_work,
        nullptr,
        idle_strategy_func,
        &idle_counter));

    aeron_err_clear();

    EXPECT_EQ(0, aeron_agent_start(&runner));
    EXPECT_EQ(AERON_AGENT_STATE_STARTED, runner.state);
    EXPECT_TRUE(runner.running);

    while (0 == idle_counter.load())
    {
        std::this_thread::yield();
    }
    EXPECT_TRUE(runner.running);

    EXPECT_EQ(0, aeron_agent_stop(&runner));
    EXPECT_EQ(AERON_AGENT_STATE_STOPPED, runner.state);
    EXPECT_FALSE(runner.running);
    EXPECT_EQ(2, call_count.load());
    EXPECT_EQ(1, idle_counter.load());
    EXPECT_EQ(0, aeron_errcode());

    EXPECT_EQ(0, aeron_agent_close(&runner));
}
