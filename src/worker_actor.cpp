/**
 * Real Time Protocol Music Instrument Digital Interface Daemon
 * Copyright (C) 2019-2026 David Moreno Montero <dmoreno@coralbits.com>
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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "worker_actor.hpp"
#include "rtpmidid/logger.hpp"

namespace rtpmididns {

worker_actor_t::worker_actor_t(actor_config_t config)
    : actor_t(std::move(config)) {
  set_drain_policy(drain_policy_t::fifo);
}

void worker_actor_t::on_control(worker_control_t &&msg) {
  if (auto *job = std::get_if<worker_job_t>(&msg)) {
    if (job->job) {
      // Blocking work runs here; the per-message isolation keeps a bad job
      // from killing the worker.
      try {
        job->job();
      } catch (const std::exception &e) {
        ERROR("Worker: job threw: {}", e.what());
      } catch (...) {
        ERROR("Worker: job threw an unknown exception");
      }
    }
  }
}

} // namespace rtpmididns
