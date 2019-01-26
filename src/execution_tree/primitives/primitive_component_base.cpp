//  Copyright (c) 2017-2018 Hartmut Kaiser
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <phylanx/config.hpp>
#include <phylanx/execution_tree/compiler/primitive_name.hpp>
#include <phylanx/execution_tree/primitives/base_primitive.hpp>
#include <phylanx/execution_tree/primitives/primitive_component_base.hpp>
#include <phylanx/util/scoped_timer.hpp>

#include <hpx/include/lcos.hpp>
#include <hpx/include/util.hpp>
#include <hpx/runtime/config_entry.hpp>
#include <hpx/runtime/launch_policy.hpp>
#include <hpx/runtime/naming_fwd.hpp>
#include <hpx/throw_exception.hpp>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <iostream>


#ifdef HPX_HAVE_APEX
#include <phylanx/util/apex_direct_vs_nondirect_policy.hpp>
#include "apex_api.hpp"
#endif

namespace phylanx { namespace execution_tree { namespace primitives
{
    ///////////////////////////////////////////////////////////////////////////
    primitive_arguments_type primitive_component_base::noargs{};

/*    ~primitive_component_base::primitive_component_base()
     {
#if defined(HPX_HAVE_APEX)
#ifdef PHYLANX_HAVE_DIRECT_VS_NONDIRECT_POLICY
        delete direct_vs_nondirect_policy_instance;
#endif    

#endif
     }
*/
    primitive_component_base::primitive_component_base(
            primitive_arguments_type&& params,
            std::string const& name, std::string const& codename,
            bool eval_direct)
      : operands_(std::move(params))
      , name_(name)
      , codename_(codename)
      , eval_count_(0ll)
      , eval_duration_(0ll)
      , execute_directly_(eval_direct ? 1 : -1)
      , measurements_enabled_(false)
    {
#if defined(HPX_HAVE_APEX)
        eval_name_ = name_ + "::eval";
#ifdef PHYLANX_HAVE_DIRECT_VS_NONDIRECT_POLICY
	if( name_ == "/phylanx/store$0/0$7$17" )
        {
		std::cout << name_ << " " << codename << std::endl;
        	direct_vs_nondirect_policy_instance = 
			std::make_unique<phylanx::util::apex_direct_vs_nondirect_policy>(name_);
	}

#endif    

#endif
    }

    namespace detail
    {
        template <typename T>
        struct keep_alive
        {
            template <typename T_>
            keep_alive(T_ && t)
              : t_(std::forward<T_>(t))
            {}

            void operator()() const {}

            T t_;
        };
    }

    template <typename T>
    detail::keep_alive<typename std::decay<T>::type> keep_alive(T && t)
    {
        return detail::keep_alive<typename std::decay<T>::type>(
            std::forward<T>(t));
    }

    hpx::future<primitive_argument_type> primitive_component_base::do_eval(
        primitive_arguments_type const& params,
        eval_context ctx) const
    {
#if defined(HPX_HAVE_APEX)
        hpx::util::annotate_function annotate(eval_name_.c_str());
#endif

        // perform measurements only when needed
        bool enable_timer = measurements_enabled_ || (execute_directly_ == -1);

        util::scoped_timer<std::int64_t> timer(eval_duration_, enable_timer);
        if (enable_timer)
        {
            ++eval_count_;
        }

        auto f = this->eval(params, std::move(ctx));

        if (enable_timer && !f.is_ready())
        {
            using shared_state_ptr =
                typename hpx::traits::detail::shared_state_ptr_for<
                    decltype(f)>::type;
            shared_state_ptr const& state =
                hpx::traits::future_access<decltype(f)>::get_shared_state(f);

            state->set_on_completed(keep_alive(std::move(timer)));
        }

        return f;
    }

    hpx::future<primitive_argument_type> primitive_component_base::do_eval(
        primitive_argument_type&& param, eval_context ctx) const
    {
#if defined(HPX_HAVE_APEX)
        hpx::util::annotate_function annotate(eval_name_.c_str());
#endif

        // perform measurements only when needed
        bool enable_timer = measurements_enabled_ || (execute_directly_ == -1);

        util::scoped_timer<std::int64_t> timer(eval_duration_, enable_timer);
        if (enable_timer)
        {
            ++eval_count_;
        }

        auto f = this->eval(std::move(param), std::move(ctx));

        if (enable_timer && !f.is_ready())
        {
            using shared_state_ptr =
                typename hpx::traits::detail::shared_state_ptr_for<
                    decltype(f)>::type;
            shared_state_ptr const& state =
                hpx::traits::future_access<decltype(f)>::get_shared_state(f);

            state->set_on_completed(keep_alive(std::move(timer)));
        }

        return f;
    }

    // eval_action
    hpx::future<primitive_argument_type> primitive_component_base::eval(
        primitive_arguments_type const& params, eval_context ctx) const
    {
        if (no_operands())
        {
            return this->eval(params, noargs, std::move(ctx));
        }
        return this->eval(this->operands(), params, std::move(ctx));
    }

    hpx::future<primitive_argument_type> primitive_component_base::eval(
        primitive_argument_type && param, eval_context ctx) const
    {
        primitive_arguments_type params;
        params.emplace_back(std::move(param));
        return this->eval(params, std::move(ctx));
    }

    hpx::future<primitive_argument_type> primitive_component_base::eval(
        primitive_arguments_type const& operands,
        primitive_arguments_type const& args, eval_context ctx) const
    {
        HPX_THROW_EXCEPTION(hpx::invalid_status,
            "phylanx::execution_tree::primitives::primitive_component_base::"
                "eval",
            generate_error_message(
                "eval function should be implemented by all primitives"));
    }

    // store_action
    void primitive_component_base::store(primitive_arguments_type&&,
        primitive_arguments_type&&)
    {
        HPX_THROW_EXCEPTION(hpx::invalid_status,
            "phylanx::execution_tree::primitives::primitive_component_base::"
                "store",
            generate_error_message(
                "store function should only be called for the primitives that "
                "support it (e.g. variables)"));
    }

    void primitive_component_base::store(primitive_argument_type&& param,
        primitive_arguments_type&& params)
    {
        primitive_arguments_type args;
        args.emplace_back(std::move(param));
        return this->store(std::move(args), std::move(params));
    }

    // extract_topology_action
    topology primitive_component_base::expression_topology(
        std::set<std::string>&& functions,
        std::set<std::string>&& resolve_children) const
    {
        std::vector<hpx::future<topology>> results;
        results.reserve(operands_.size());

        for (auto& operand : operands_)
        {
            primitive const* p = util::get_if<primitive>(&operand);
            if (p != nullptr)
            {
                std::set<std::string> funcs{functions};
                std::set<std::string> resolve{resolve_children};
                results.push_back(p->expression_topology(
                    std::move(funcs), std::move(resolve)));
            }
        }

        std::vector<topology> children;
        if (!results.empty())
        {
            hpx::wait_all(results);

            for (auto& r : results)
            {
                children.emplace_back(r.get());
            }
        }

        return topology{std::move(children)};
    }

    // bind_action
    bool primitive_component_base::bind(
        primitive_arguments_type const& params, eval_context ctx) const
    {
        HPX_THROW_EXCEPTION(hpx::invalid_status,
            "phylanx::execution_tree::primitives::"
                "primitive_component_base::bind",
            generate_error_message(
                "bind function should only be called for the "
                    "primitives that support it (e.g. variable/function)"));
        return true;
    }

    std::string primitive_component_base::generate_error_message(
        std::string const& msg) const
    {
        return util::generate_error_message(msg, name_, codename_);
    }

    std::int64_t primitive_component_base::get_eval_count(bool reset) const
    {
        return hpx::util::get_and_reset_value(eval_count_, reset);
    }

    std::int64_t primitive_component_base::get_eval_duration(bool reset) const
    {
        return hpx::util::get_and_reset_value(eval_duration_, reset);
    }

    std::int64_t primitive_component_base::get_direct_execution(bool reset) const
    {
        return hpx::util::get_and_reset_value(execute_directly_, reset);
    }

    void primitive_component_base::enable_measurements()
    {
        measurements_enabled_ = true;
    }

    ////////////////////////////////////////////////////////////////////////////
    // decide whether to execute eval directly
    bool primitive_component_base::get_sync_execution()
    {
        static bool sync_execution =
            hpx::get_config_entry("phylanx.sync_execution", "0") == "1";
        return sync_execution;
    }

    // get eval count from command line
    std::int64_t primitive_component_base::get_ec_threshold()
    {
        static std::int64_t ec_threshold = std::stol(
            hpx::get_config_entry("phylanx.eval_count_threshold", "5"));
        return ec_threshold;
    }

    // get execution time upper threshold from command line
    std::int64_t primitive_component_base::get_exec_upper_threshold()
    {
        static std::int64_t exec_upper_threshold =
            std::stol(hpx::get_config_entry(
                "phylanx.exec_time_upper_threshold",
/* What's going on here?  Well, direct actions cause problems on POWER8
 * with Clang 5.0. That's because the call stack gets too deep.  Changing
 * this threshold to 0 will disable direct actions on that platform.
 * There is also a github issue #584 that explains this in detail. */
#if defined(__POWERPC__) && defined(__clang_version__)
                "0"
#else
                "500000"
#endif
                ));
        return exec_upper_threshold;
    }

    // get execution time lower threshold from command line
    std::int64_t primitive_component_base::get_exec_lower_threshold()
    {
        static std::int64_t exec_lower_threshold =
            std::stol(hpx::get_config_entry(
                "phylanx.exec_time_lower_threshold", "350000"));
        return exec_lower_threshold;
    }

    hpx::launch primitive_component_base::select_direct_eval_execution(
        hpx::launch policy) const
    {
        // always run this on an HPX thread
        if (hpx::threads::get_self_ptr() == nullptr)
        {
            return hpx::launch::async;
        }

        // always execute synchronously, if requested
        if (get_sync_execution())
        {
            return hpx::launch::sync;
        }

        if ((eval_count_ != 0 && measurements_enabled_) ||
            (eval_count_ > get_ec_threshold()))
        {
            // check whether execution status needs to be changed (with some
            // hysteresis)
            std::int64_t exec_time = (eval_duration_ / eval_count_);
            if (exec_time > get_exec_upper_threshold())
            {
                execute_directly_ = 0;
            }
            else if (exec_time < get_exec_lower_threshold())
            {
                execute_directly_ = 1;
            }
            else
            {
                execute_directly_ = -1;
            }
        }

        if (execute_directly_ == 1)
        {
            return hpx::launch::sync;
        }
        else if (execute_directly_ == 0)
        {
            return hpx::launch::async;
        }

        return policy;
    }
}}}
