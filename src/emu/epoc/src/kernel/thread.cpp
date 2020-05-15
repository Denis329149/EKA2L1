/*
 * Copyright (c) 2018 EKA2L1 Team / Citra Team
 * 
 * This file is part of EKA2L1 project / Citra Emulator Project
 * (see bentokun.github.com/EKA2L1).
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <common/algorithm.h>
#include <common/cvt.h>
#include <common/log.h>
#include <common/random.h>

#include <epoc/kernel.h>
#include <epoc/kernel/mutex.h>
#include <epoc/kernel/sema.h>
#include <epoc/kernel/thread.h>
#include <epoc/mem.h>
#include <epoc/ptr.h>
#include <epoc/utils/err.h>

namespace eka2l1 {
    namespace kernel {
        struct epoc9_thread_create_info {
            int handle;
            int type;
            address func_ptr;
            address ptr;
            address supervisor_stack;
            int supervisor_stack_size;
            address user_stack;
            int user_stack_size;
            int init_thread_priority;
            uint32_t name_len;
            address name_ptr;
            int total_size;
        };

        struct epoc9_std_epoc_thread_create_info : public epoc9_thread_create_info {
            address allocator;
            int heap_min;
            int heap_max;
            int padding;
        };

        int map_thread_priority_to_calc(thread_priority pri) {
            switch (pri) {
            case thread_priority::priority_much_less:
                return -7;

            case thread_priority::priority_less:
                return -6;

            case thread_priority::priority_normal:
                return -5;

            case thread_priority::priority_more:
                return -4;

            case thread_priority::priority_much_more:
                return -3;

            case thread_priority::priority_real_time:
                return -2;

            case thread_priority::priority_null:
                return 0;

            case thread_priority::priority_absolute_very_low:
                return 1;

            case thread_priority::priority_absolute_low:
                return 5;

            case thread_priority::priority_absolute_background_normal:
                return 7;

            case thread_priority::priority_absolute_background:
                return 10;

            case thread_priority::priority_absolute_foreground_normal:
                return 12;

            case thread_priority::priorty_absolute_foreground:
                return 15;

            case thread_priority::priority_absolute_high:
                return 23;

            default: {
                LOG_WARN("Undefined priority.");
                break;
            }
            }

            return 1;
        }

        int map_process_pri_calc(process_priority pri) {
            switch (pri) {
            case process_priority::low:
                return 0;
            case process_priority::background:
                return 1;
            case process_priority::foreground:
                return 2;
            case process_priority::high:
                return 3;
            case process_priority::window_svr:
                return 4;
            case process_priority::file_svr:
                return 5;
            case process_priority::supervisor:
                return 6;
            case process_priority::real_time_svr:
                return 7;
            default: {
                LOG_WARN("Undefined process priority");
                break;
            }
            }

            return 0;
        }

        int calculate_thread_priority(kernel::process *pr, thread_priority pri) {
            const uint8_t pris[] = {
                1, 1, 2, 3, 4, 5, 22, 0,
                3, 5, 6, 7, 8, 9, 22, 0,
                3, 10, 11, 12, 13, 14, 22, 0,
                3, 17, 18, 19, 20, 22, 23, 0,
                9, 15, 16, 21, 24, 25, 28, 0,
                9, 15, 16, 21, 24, 25, 28, 0,
                9, 15, 16, 21, 24, 25, 28, 0,
                18, 26, 27, 28, 29, 30, 31, 0
            };

            int tp = map_thread_priority_to_calc(pri);

            if (tp >= 0) {
                return (tp < 64) ? tp : 63;
            }

            int prinew = tp + 8;

            if (prinew < 0)
                prinew = 0;

            int idx = (map_process_pri_calc(pr->get_priority()) << 3) + static_cast<int>(prinew);
            return pris[idx];
        }

        void thread::push_call(const std::string &func_name,
            const arm::arm_interface::thread_context &ctx) {
            call_stacks.push({ ctx, func_name });
        }

        void thread::pop_call() {
            call_stacks.pop();
        }

        void thread::reset_thread_ctx(const std::uint32_t entry_point, const std::uint32_t stack_top, const std::uint32_t thr_local_data_ptr,
            const bool initial) {
            std::fill(ctx.cpu_registers.begin(), ctx.cpu_registers.end(), 0);
            std::fill(ctx.fpu_registers.begin(), ctx.fpu_registers.end(), 0);

            /* Userland process and thread are all initialized with _E32Startup, which is the first
               entry point of an process. _E32Startup required:
               - r1: thread creation info register.
               - r4: Startup reason. Thread startup is 1, process startup is 0.
            */

            ctx.pc = owner ? (initial ? entry_point : owning_process()->get_entry_point_address())
                           : entry_point;

            ctx.sp = stack_top;
            ctx.cpsr = ((ctx.pc & 1) << 5);

            ctx.cpu_registers[1] = stack_top;

            if (!initial) {
                // Thread initialization, not process
                ctx.cpu_registers[4] = 1;
            }

            ctx.wrwr = thr_local_data_ptr;
        }

        void thread::create_stack_metadata(std::uint8_t *stack_host_ptr, address stack_ptr, ptr<void> allocator,
            uint32_t name_len, address name_ptr, const address epa) {
            epoc9_std_epoc_thread_create_info info;
            info.allocator = allocator.ptr_address();
            info.func_ptr = epa;
            info.ptr = usrdata.ptr_address();

            // The handle to RThread. HLE function ignore this, however when a RThread HLE call is executed, this
            // will point to that RThread Handle
            info.handle = 0;
            info.heap_min = min_heap_size;
            info.heap_max = max_heap_size;
            info.init_thread_priority = priority;

            info.name_len = name_len;
            info.name_ptr = name_ptr;
            info.supervisor_stack = 0;
            info.supervisor_stack_size = 0;

            info.user_stack = stack_ptr;
            info.user_stack_size = stack_size;
            info.padding = 0;

            info.total_size = 0x40;

            memcpy(stack_host_ptr, &info, 0x40);
        }

        thread::thread(kernel_system *kern, memory_system *mem, ntimer *timing, kernel::process *owner,
            kernel::access_type access,
            const std::string &name, const address epa, const size_t stack_size,
            const size_t min_heap_size, const size_t max_heap_size,
            bool initial,
            ptr<void> usrdata,
            ptr<void> allocator,
            thread_priority pri)
            : kernel_obj(kern, name, reinterpret_cast<kernel_obj *>(owner), access)
            , stack_size(static_cast<int>(stack_size))
            , min_heap_size(static_cast<int>(min_heap_size))
            , max_heap_size(static_cast<int>(max_heap_size))
            , usrdata(usrdata)
            , mem(mem)
            , priority(pri)
            , timing(timing)
            , timeout_sts(0)
            , wait_obj(nullptr)
            , sleep_nof_sts(0)
            , thread_handles(kern, handle_array_owner::thread)
            , rendezvous_reason(0)
            , exit_reason(0)
            , exit_type(entity_exit_type::pending)
            , create_time(0)
            , exception_handler(0)
            , exception_mask(0) {
            if (owner) {
                owner->increase_thread_count();
                real_priority = calculate_thread_priority(owning_process(), pri);
                last_priority = real_priority;
            }

            create_time = timing->ticks();

            timeslice = 20000;
            time = 20000;

            obj_type = object_type::thread;
            state = thread_state::create; // Suspended.

            /* Here, since reschedule is needed for switching thread and process, primary thread handle are owned by kernel. */

            stack_chunk = kern->create<kernel::chunk>(kern->get_memory_system(), owning_process(), "", 0, static_cast<std::uint32_t>(common::align(stack_size, mem->get_page_size())), common::align(stack_size, mem->get_page_size()), prot::read_write,
                chunk_type::normal, chunk_access::local, chunk_attrib::none, false);

            name_chunk = kern->create<kernel::chunk>(kern->get_memory_system(), owning_process(), "", 0, static_cast<std::uint32_t>(common::align(name.length() * 2 + 4, mem->get_page_size())), common::align(name.length() * 2 + 4, mem->get_page_size()), prot::read_write,
                chunk_type::normal, chunk_access::local, chunk_attrib::none, false);

            request_sema = kern->create<kernel::semaphore>("requestSema" + common::to_string(eka2l1::random()), 0);

            sync_msg = kern->create_msg(owner_type::kernel);
            sync_msg->lock_free();

            /* Create TDesC string. Combine of string length and name data (USC2) */

            std::u16string name_16(name.begin(), name.end());
            memcpy(name_chunk->host_base(), name_16.data(), name.length() * 2);

            // TODO: Not hardcode this
            const size_t metadata_size = 0x40;

            std::uint8_t *stack_beg_meta_ptr = reinterpret_cast<std::uint8_t *>(stack_chunk->host_base());
            std::uint8_t *stack_top_ptr = stack_beg_meta_ptr + stack_size - metadata_size;

            const address stack_top = stack_chunk->base(owner).ptr_address() + static_cast<address>(stack_size - metadata_size);

            // Fill the stack with garbage
            std::fill(stack_beg_meta_ptr, stack_top_ptr, 0xcc);
            create_stack_metadata(stack_top_ptr, stack_top, allocator, static_cast<std::uint32_t>(name.length()),
                name_chunk->base(owner).ptr_address(), epa);

            // Create local data chunk
            // Alloc extra the size of thread local data to avoid dealing with binary compatibility (size changed etc...)
            local_data_chunk = kern->create<kernel::chunk>(kern->get_memory_system(), owning_process(), "", 0, 0x1000, 0x1000,
                prot::read_write, chunk_type::normal, chunk_access::local, chunk_attrib::none, false);

            if (local_data_chunk) {
                std::uint8_t *data = reinterpret_cast<std::uint8_t *>(local_data_chunk->host_base());

                ldata = reinterpret_cast<thread_local_data *>(data);
                new (ldata) thread_local_data();

                ldata->heap = 0;
                ldata->scheduler = 0;
                ldata->trap_handler = 0;
                ldata->thread_id = 0;
                ldata->tls_heap = 0;
            }

            reset_thread_ctx(epa, stack_top, local_data_chunk->base(owner).ptr_address(), initial);
            scheduler = kern->get_thread_scheduler();

            // Add thread to process's thread list
            owner->get_thread_list().push(&process_thread_link);
        }

        void thread::destroy() {
            // Unlink from proces's thread list
            process_thread_link.deque();
            owning_process()->decrease_thread_count();
        }

        tls_slot *thread::get_tls_slot(uint32_t handle, uint32_t dll_uid) {
            for (uint32_t i = 0; i < ldata->tls_slots.size(); i++) {
                if (ldata->tls_slots[i].handle != -1 && ldata->tls_slots[i].handle == handle) {
                    return &ldata->tls_slots[i];
                }
            }

            for (uint32_t i = 0; i < ldata->tls_slots.size(); i++) {
                if (ldata->tls_slots[i].handle == -1) {
                    ldata->tls_slots[i].handle = handle;
                    ldata->tls_slots[i].uid = dll_uid;

                    return &ldata->tls_slots[i];
                }
            }

            return nullptr;
        }

        void thread::close_tls_slot(tls_slot &slot) {
            slot.handle = -1;
        }

        bool thread::sleep(uint32_t mssecs, const bool deque) {
            return scheduler->sleep(this, mssecs, deque);
        }

        bool thread::sleep_nof(eka2l1::ptr<epoc::request_status> sts, uint32_t mssecs) {
            assert(!sleep_nof_sts && "Thread supposed to sleep already");
            sleep_nof_sts = sts;

            return scheduler->sleep(this, mssecs, false);
        }

        void thread::notify_sleep(const int errcode) {
            kern->lock();

            if (sleep_nof_sts) {
                *(sleep_nof_sts.get(owning_process())) = errcode;
                sleep_nof_sts = 0;

                signal_request();
            }

            sleep_nof_sts = 0;
            kern->unlock();
        }

        bool thread::stop() {
            return scheduler->stop(this);
        }

        void thread::update_priority() {
            last_priority = real_priority;

            bool should_reschedule_back = false;

            if (scheduler_link.next != nullptr && scheduler_link.previous != nullptr) {
                // It's in the queue!!! Move it!
                scheduler->dequeue_thread_from_ready(this);
                should_reschedule_back = true;
            }

            real_priority = calculate_thread_priority(owning_process(), priority);

            if (should_reschedule_back) {
                // It's in the queue!!! Move it!
                scheduler->queue_thread_ready(this);
            }

            if (wait_obj) {
                switch (wait_obj->get_object_type()) {
                case object_type::mutex: {
                    reinterpret_cast<mutex *>(wait_obj)->priority_change(this);
                    break;
                }

                case object_type::sema: {
                    reinterpret_cast<semaphore *>(wait_obj)->priority_change();
                    break;
                }

                default:
                    break;
                }
            }
        }

        void thread::set_priority(const thread_priority new_pri) {
            priority = new_pri;
            update_priority();
            kern->prepare_reschedule();
        }

        void thread::wait_for_any_request() {
            request_sema->wait();
        }

        void thread::signal_request(int count) {
            request_sema->signal(count);
        }

        bool thread::suspend() {
            bool res = scheduler->wait(this);

            if (!res) {
                return false;
            }

            res = false;
            state = thread_state::wait;

            // Call wait object to handle suspend event
            if (wait_obj) {
                switch (wait_obj->get_object_type()) {
                case object_type::mutex: {
                    res = reinterpret_cast<mutex *>(wait_obj)->suspend_thread(this);
                    break;
                }

                case object_type::sema: {
                    res = reinterpret_cast<semaphore *>(wait_obj)->suspend_waiting_thread(this);
                    break;
                }

                default:
                    break;
                }
            }

            return res;
        }

        bool thread::resume() {
            bool res = scheduler->resume(this);

            if (!res) {
                return false;
            }

            res = false;
            state = thread_state::ready;

            // Call wait object to handle suspend event
            if (wait_obj) {
                switch (wait_obj->get_object_type()) {
                case object_type::mutex: {
                    res = reinterpret_cast<mutex *>(wait_obj)->unsuspend_thread(this);
                    break;
                }

                case object_type::sema: {
                    res = reinterpret_cast<semaphore *>(wait_obj)->unsuspend_waiting_thread(this);
                    break;
                }

                default:
                    break;
                }
            }

            return res;
        }

        void thread::owning_process(kernel::process *pr) {
            owner = reinterpret_cast<kernel_obj *>(pr);
            owning_process()->increase_thread_count();

            name_chunk->set_owner(pr);
            stack_chunk->set_owner(pr);

            update_priority();
            last_priority = real_priority;
        }

        kernel_obj_ptr thread::get_object(uint32_t handle) {
            return thread_handles.get_object(handle);
        }

        void thread::logon(eka2l1::ptr<epoc::request_status> logon_request, bool rendezvous) {
            if (state == thread_state::stop) {
                *(logon_request.get(kern->crr_process())) = exit_reason;
                return;
            }

            if (rendezvous) {
                rendezvous_requests.emplace_back(logon_request, kern->crr_thread());
                return;
            }

            logon_requests.emplace_back(logon_request, kern->crr_thread());
        }

        bool thread::logon_cancel(eka2l1::ptr<epoc::request_status> logon_request, bool rendezvous) {
            if (rendezvous) {
                auto req_info = std::find_if(rendezvous_requests.begin(), rendezvous_requests.end(),
                    [&](epoc::notify_info &form) { return form.sts.ptr_address() == logon_request.ptr_address(); });

                if (req_info != rendezvous_requests.end()) {
                    (*req_info).complete(-3);
                    rendezvous_requests.erase(req_info);

                    return true;
                }

                return false;
            }

            auto req_info = std::find_if(logon_requests.begin(), logon_requests.end(),
                [&](epoc::notify_info &form) { return form.sts.ptr_address() == logon_request.ptr_address(); });

            if (req_info != logon_requests.end()) {
                (*req_info).complete(-3);
                logon_requests.erase(req_info);

                return true;
            }

            return false;
        }

        void thread::rendezvous(int rendezvous_reason) {
            for (auto &ren : rendezvous_requests) {
                ren.complete(rendezvous_reason);
            }

            rendezvous_requests.clear();
        }

        void thread::finish_logons() {
            for (auto &req : logon_requests) {
                req.complete(exit_reason);
            }

            for (auto &req : rendezvous_requests) {
                req.complete(exit_reason);
            }

            logon_requests.clear();
            rendezvous_requests.clear();
        }

        chunk_ptr thread::get_stack_chunk() {
            return stack_chunk;
        }

        void thread::add_ticks(const int num) {
            time = common::max(0, time - num);
        }
    }
}
