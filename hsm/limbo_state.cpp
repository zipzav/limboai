/**
 * limbo_state.cpp
 * =============================================================================
 * Copyright (c) 2023-present Serhii Snitsaruk and the LimboAI contributors.
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 * =============================================================================
 */

#include "limbo_state.h"

#include "scene/main/window.h"

#ifdef LIMBOAI_MODULE
#include "core/config/engine.h"
#endif // LIMBOAI_MODULE

#ifdef LIMBOAI_GDEXTENSION
#include <godot_cpp/classes/engine.hpp>
#endif

void LimboState::restart() {
	ERR_FAIL_COND(!active);

	_exit();
	_enter();
}

void LimboState::set_blackboard_plan(const Ref<BlackboardPlan> &p_plan) {
	if (blackboard_plan == p_plan) {
		return;
	}

	blackboard_plan = p_plan;

	if (Engine::get_singleton()->is_editor_hint() && blackboard_plan.is_valid()) {
		blackboard_plan->set_parent_scope_plan_provider(Callable(this, "_get_parent_scope_plan"));
	}

	_update_blackboard_plan();
}

void LimboState::_update_blackboard_plan() {
}

Node *LimboState::_get_prefetch_root_for_base_plan() {
	return this;
}

Ref<BlackboardPlan> LimboState::_get_parent_scope_plan() const {
	BlackboardPlan *parent_plan = nullptr;
	const LimboState *state = this;
	while (state->get_parent() && IS_CLASS(state->get_parent(), LimboState)) {
		state = Object::cast_to<LimboState>(state->get_parent());
		ERR_FAIL_NULL_V(state, parent_plan);
		if (state->blackboard_plan.is_valid()) {
			parent_plan = state->blackboard_plan.ptr();
			break;
		}
	}
	return parent_plan;
}

LimboState *LimboState::get_root() const {
	const Node *state = this;
	while (state->get_parent() && IS_CLASS(state->get_parent(), LimboState)) {
		state = state->get_parent();
	}
	return const_cast<LimboState *>(Object::cast_to<LimboState>(state));
}

LimboState *LimboState::named(const String &p_name) {
	set_name(p_name);
	return this;
}

void LimboState::_enter() {
	active = true;
	GDVIRTUAL_CALL(_enter);

	for (Variant action : on_enter_actions)
	{
		Ref<ExecutionContext> context(memnew(ExecutionContext));
		context->set_root(get_tree()->get_root()->get_child(0));
		Object::cast_to<Action>(action)->execute(context);
	}

	emit_signal(LW_NAME(entered));
}

void LimboState::_exit() {
	if (!active) {
		return;
	}
	GDVIRTUAL_CALL(_exit);

	for (Variant action : on_enter_actions)
	{
		Action* action_ptr = Object::cast_to<Action>(action);
		if(!action_ptr->_is_revertible())
			continue;

		Ref<ExecutionContext> context(memnew(ExecutionContext));
		context->set_root(get_tree()->get_root()->get_child(0));
		action_ptr->revert(context);
	}

	emit_signal(LW_NAME(exited));
	active = false;
}

void LimboState::_update(double p_delta) {
	GDVIRTUAL_CALL(_update, p_delta);
	emit_signal(LW_NAME(updated), p_delta);
}

void LimboState::_setup() {
	GDVIRTUAL_CALL(_setup);
	emit_signal(LW_NAME(setup));
}

void LimboState::_initialize(Node *p_agent, const Ref<Blackboard> &p_blackboard) {
	ERR_FAIL_COND(p_agent == nullptr);
	agent = p_agent;

	if (_should_use_new_scope()) {
		blackboard->set_parent(p_blackboard);
	} else {
		blackboard = p_blackboard;
	}
	if (blackboard_plan.is_valid() && !blackboard_plan->is_empty()) {
		// Don't overwrite existing blackboard values as they may be initialized from code.
		blackboard_plan->populate_blackboard(blackboard, false, this, _get_prefetch_root_for_base_plan());
	}

	_setup();
}

bool LimboState::_dispatch(const StringName &p_event, const Variant &p_cargo) {
	ERR_FAIL_COND_V(p_event == StringName(), false);
	if (handlers.size() > 0 && handlers.has(p_event)) {
		Variant ret;

#ifdef LIMBOAI_MODULE
		Callable::CallError ce;
		if (p_cargo.get_type() == Variant::NIL) {
			handlers[p_event].callp(nullptr, 0, ret, ce);
			if (ce.error != Callable::CallError::CALL_OK) {
				ERR_PRINT("Error calling event handler " + Variant::get_callable_error_text(handlers[p_event], nullptr, 0, ce));
			}
		} else {
			const Variant *argptrs[1];
			argptrs[0] = &p_cargo;
			handlers[p_event].callp(argptrs, 1, ret, ce);
			if (ce.error != Callable::CallError::CALL_OK) {
				ERR_PRINT("Error calling event handler " + Variant::get_callable_error_text(handlers[p_event], argptrs, 1, ce));
			}
		}

#elif LIMBOAI_GDEXTENSION
		if (p_cargo.get_type() == Variant::NIL) {
			ret = handlers[p_event].call();
		} else {
			Array args;
			args.append(p_cargo);
			ret = handlers[p_event].callv(args);
		}
#endif // LIMBOAI_GDEXTENSION

		if (unlikely(ret.get_type() != Variant::BOOL)) {
			ERR_PRINT("Event handler returned unexpected type: " + Variant::get_type_name(ret.get_type()));
		} else {
			return ret;
		}
	}
	return false;
}

void LimboState::add_event_handler(const StringName &p_event, const Callable &p_handler) {
	ERR_FAIL_COND(p_event == StringName());
	ERR_FAIL_COND(!p_handler.is_valid());
	handlers.insert(p_event, p_handler);
}

bool LimboState::dispatch(const StringName &p_event, const Variant &p_cargo) {
	return get_root()->_dispatch(p_event, p_cargo);
}

LimboState *LimboState::call_on_enter(const Callable &p_callable) {
	ERR_FAIL_COND_V(!p_callable.is_valid(), this);
	connect(LW_NAME(entered), p_callable);
	return this;
}

LimboState *LimboState::call_on_exit(const Callable &p_callable) {
	ERR_FAIL_COND_V(!p_callable.is_valid(), this);
	connect(LW_NAME(exited), p_callable);
	return this;
}

LimboState *LimboState::call_on_update(const Callable &p_callable) {
	ERR_FAIL_COND_V(!p_callable.is_valid(), this);
	connect(LW_NAME(updated), p_callable);
	return this;
}

void LimboState::set_guard(const Callable &p_guard_callable) {
	ERR_FAIL_COND(!p_guard_callable.is_valid());
	guard_callable = p_guard_callable;
}

void LimboState::clear_guard() {
	guard_callable = Callable();
}

void LimboState::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (Engine::get_singleton()->is_editor_hint()) {
				_update_blackboard_plan();
			}
		} break;
	}
}

void LimboState::set_on_enter_actions(const TypedArray<Action> &new_actions) {
	on_enter_actions = new_actions;
}

void LimboState::set_on_exit_actions(const TypedArray<Action> &new_actions) {
	on_exit_actions = new_actions;
}

void LimboState::_bind_methods() {
	ClassDB::bind_method(D_METHOD("restart"), &LimboState::restart);
	ClassDB::bind_method(D_METHOD("get_root"), &LimboState::get_root);
	ClassDB::bind_method(D_METHOD("get_agent"), &LimboState::get_agent);
	ClassDB::bind_method(D_METHOD("set_agent", "agent"), &LimboState::set_agent);
	ClassDB::bind_method(D_METHOD("event_finished"), &LimboState::event_finished);
	ClassDB::bind_method(D_METHOD("is_active"), &LimboState::is_active);
	ClassDB::bind_method(D_METHOD("_initialize", "agent", "blackboard"), &LimboState::_initialize);
	ClassDB::bind_method(D_METHOD("dispatch", "event", "cargo"), &LimboState::dispatch, Variant());
	ClassDB::bind_method(D_METHOD("named", "name"), &LimboState::named);
	ClassDB::bind_method(D_METHOD("add_event_handler", "event", "handler"), &LimboState::add_event_handler);
	ClassDB::bind_method(D_METHOD("call_on_enter", "callable"), &LimboState::call_on_enter);
	ClassDB::bind_method(D_METHOD("call_on_exit", "callable"), &LimboState::call_on_exit);
	ClassDB::bind_method(D_METHOD("call_on_update", "callable"), &LimboState::call_on_update);
	ClassDB::bind_method(D_METHOD("set_guard", "guard_callable"), &LimboState::set_guard);
	ClassDB::bind_method(D_METHOD("clear_guard"), &LimboState::clear_guard);
	ClassDB::bind_method(D_METHOD("get_blackboard"), &LimboState::get_blackboard);

	ClassDB::bind_method(D_METHOD("set_blackboard_plan", "plan"), &LimboState::set_blackboard_plan);
	ClassDB::bind_method(D_METHOD("get_blackboard_plan"), &LimboState::get_blackboard_plan);

	ClassDB::bind_method(D_METHOD("_get_parent_scope_plan"), &LimboState::_get_parent_scope_plan);

	ClassDB::bind_method(D_METHOD("set_on_enter_actions"), &LimboState::set_on_enter_actions);
	ClassDB::bind_method(D_METHOD("get_on_enter_actions"), &LimboState::get_on_enter_actions);

	ClassDB::bind_method(D_METHOD("set_on_exit_actions"), &LimboState::set_on_exit_actions);
	ClassDB::bind_method(D_METHOD("get_on_exit_actions"), &LimboState::get_on_exit_actions);

	GDVIRTUAL_BIND(_setup);
	GDVIRTUAL_BIND(_enter);
	GDVIRTUAL_BIND(_exit);
	GDVIRTUAL_BIND(_update, "delta");

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "EVENT_FINISHED", PROPERTY_HINT_NONE, ""), "", "event_finished");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "agent", PROPERTY_HINT_RESOURCE_TYPE, "Node"), "set_agent", "get_agent");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "blackboard", PROPERTY_HINT_RESOURCE_TYPE, "Blackboard"), "", "get_blackboard");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "blackboard_plan", PROPERTY_HINT_RESOURCE_TYPE, "BlackboardPlan", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_ALWAYS_DUPLICATE), "set_blackboard_plan", "get_blackboard_plan");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "on_enter_actions", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("Action")), "set_on_enter_actions", "get_on_enter_actions");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "on_exit_actions", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("Action")), "set_on_exit_actions", "get_on_exit_actions");


	ADD_SIGNAL(MethodInfo("setup"));
	ADD_SIGNAL(MethodInfo("entered"));
	ADD_SIGNAL(MethodInfo("exited"));
	ADD_SIGNAL(MethodInfo("updated", PropertyInfo(Variant::FLOAT, "delta")));
}

LimboState::LimboState() {
	EVENT_FINISHED = StringName("finished_" + itos(get_instance_id()));
	agent = nullptr;
	active = false;
	blackboard = Ref<Blackboard>(memnew(Blackboard));

	guard_callable = Callable();

	set_process(false);
	set_physics_process(false);
	set_process_input(false);
	set_process_unhandled_input(false);
}
