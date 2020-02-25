/*
 * Copyright (c) 2020 EKA2L1 Team
 * 
 * This file is part of EKA2L1 project.
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

#pragma once

#include <epoc/services/window/classes/wsobj.h>

#include <queue>
#include <string>

namespace eka2l1::epoc {
    struct screen;
    struct window_user;

    struct dsa : public window_client_obj {
        window_user *husband_;              ///< Mmmhhh
        
        enum state {
            state_none,
            state_in_progress,
            state_completed
        } state_;

        explicit dsa(window_server_client_ptr client);

        void request_access(eka2l1::service::ipc_context &ctx, eka2l1::ws_cmd &cmd);
        void execute_command(eka2l1::service::ipc_context &ctx, eka2l1::ws_cmd &cmd) override;
    };
}