# 2026.0611.0155 plaintext payload: static field inventory

## Scope and evidence

Source analysed: `逆向存档/2026.0611.0155/hook_so/full_extracted_script.py`.

- Extracted Python size: 580,392 bytes.
- Top-level functions: 365.
- Literal `getattr(..., 'name')` occurrences: 633.
- The source uses direct attributes, literal fallback-name tuples, direct methods, and broad `try/except: pass` blocks. A successful static read proves the payload **tries** a field; it does not prove that every fallback name was present in every build.

The payload is plainly recoverable Python rather than encrypted business logic: imports, comments, class names, object paths, attribute names, packet schemas, and debugging strings are all retained as text. The compiled loader may be native code, but the data-collection logic described here is inspectable with an ordinary text search.

## Object roots and traversal

| Root/path | Role in the payload |
|---|---|
| `GK.g_unit` | Local unit root; used for player, room, UID, items, QTE and self-state access. |
| `GK.g_world_mgr.battle_world` / `cur_world` | Battle world, scene and timing source. |
| `GK.g_unit.room.unit_mgr` | Primary entity-manager route; alternative managers are also tried. |
| `room.combat_core.ref_unit_mgr` | Alternate manager and mode-specific systems. |
| `GK.input` | Primary QTE UI object. |
| `GK.g_ui_mgr` → `battle_ui.layer_proxy` | Fallback UI route to `UIBattleQTE`. |
| `GK.DM` | Data-table root, including `goldrush_item_data`. |

## Confirmed normal-match collection paths

### Identity, player state, inventory, talents

Direct identity fields are `uid`, `pid`, `unit_type`, `is_butcher`, `is_civilian`, `player_name`, `position`, and `state_machine.state`. UID resolution first reads `uid`, then falls back to `warm_info['unique_id']` and `unique_id`.

The talent path is a direct read of `player.genius_id_lv_lst`. Entries are interpreted as either an ID or an `(id, level)` pair. The payload's own mapping labels IDs 8/16/24/32 as `封窗/底牌/一刀/张狂` for boss units and `飞轮/心脏/搏命/双弹` for survivor units. This mapping is payload-local interpretation; the direct evidence is the `genius_id_lv_lst` read.

Inventory uses `item_lst`, with `combat_unit.get_hold_items()` as an alternate route. Item fields include `type`, `item_id`, `item_idx`, `item_info`, `item_num`, `num`, `count`, `_remain_use_times`, `_remain_lifespan`, `_init_lifespan`, `consume_cnt`, `b_in_use`, `item_cd_time`, `life_cycle`, and `data`. Skill fields include `skill_mgr`, `skill_dict`/`skill_dct`, `current_skill_ex`, `skill_id`, `skill_type`, `cd_delta`, `is_casting`, `logic`, and `ref_cd_comp`.

### Transform, model, and bones

The position and direction paths are `get_position3()` / `get_direction3()` and vector members `x`, `y`, `z`. Model discovery uses `model.valid`, `model.filename`, then the fallback names `file_name`, `filepath`, `full_path`, `res_path`, `resource_path`, and `prefab_path`.

Bone collection requires `model` and `anim_data_component`, then calls `anim_data_component.get_bone_position(bone_name=...)`. The literal target list is:

`biped spine`, `biped head`, `biped neck`, `biped l clavicle`, `biped r clavicle`, `biped l upperarm`, `biped r upperarm`, `biped l forearm`, `biped r forearm`, `biped l hand`, `biped r hand`, `biped l thigh`, `biped r thigh`, `biped l calf`, `biped r calf`, `biped l foot`, `biped r foot`.

The output schema is `{"type":"bones","self_uid":...,"data":{uid:{bone:{"world":[x,y,z],"screen":[0,0],"visible":true}}}}`.

### Objective progress and map objects

| Object source | Direct fields/methods | Emitted meaning |
|---|---|---|
| `get_generator_units()` | `fix_process`, `get_position3()` | Cipher-machine position and progress. |
| `get_door_units()` | `hack_process`, `get_position3()` | Gate position and progress. |
| `room.region_mgr` | `get_basement_count(True)`, `get_basement_enter_pos(True, idx=...)` | Open basement count and entrance positions. |
| `get_panel_units()` | `uid`, state or `_state_comp`, `span_flag`, `oneway_tag`, position/direction | Pallet state and geometry. |
| `get_hook_units()` | chair/hook state and runtime fields below | Chair state and countdown data. |
| `get_wood_units()` | `uid`, position, direction | Window positions and directions. |

Pallet state is read from `state`, `current_state`, `cur_state`, or the same fields inside `_state_comp`. `PANEL_STATE` names used by the payload are `IDLE`, `USED`, `USEING`, `SPAN`, and `DESTROY`.

Chair data is deliberately heuristic. It tries `hang_uid`, `hook_uid`, `target_uid`, `hook_target_uid`, `chair_target_uid`; `hook_value`, `cur_hook_value`, `current_hook_value`; `sit_chair_num`, `hook_frequency`, `hook_count`, `sit_count`; `kill_speed`, `hook_kill_speed`, `get_kill_speed`; `kill_speed_factor`, `hook_kill_speed_factor`; and `hang_time`, `hook_time`, `start_hook_time`. It derives payload-local `hook_progress_percent`, `left_time_seconds`, `absolute_left_time_seconds`, and `dynamic_hook_value` from those reads. The derived keys are not engine fields.

### Buffs and special player states

Unit Buff discovery reads `buff_mgr`, then `buff_dct`; fallbacks are `get_buff_list()`, `_buff_list`, and `get_buff()`. Individual Buff values include `data`, `level`, `layer`, `left_time`, and timing alternatives `get_existence_time`, `remain_time`, `remaining_time`, `duration`, `_duration`, `start_time`, and `begin_time`. Configuration dictionaries may expose `buff_name`, `is_fragrance_can_back`, and `special_time`.

The fragrance collector reads `is_using_fragrance`, `_fragrance_image`, `_fragrance_image_uid`, `need_create_image_again`, `_fragrance_alpha_speed`, `fragrance_delay_cancel`, `fragrance_dissolve_handler`, and item fields `b_in_use`, `item_cd_time`, `_remain_lifespan`, `_init_lifespan`, `life_cycle`, and `data`. It also installs payload-local wrappers around `set_struggle`, `set_struggle_value_only`, `set_success_struggle`, and `recover_state_kill` to cache values provided by the normal event flow. Those wrapper names are methods, not passive fields.

Other direct special-state fields are `marionette_mgr.marionette_hp`, `marionette_hp_max`, `change_progress_acc`, `marionette_retention_duration`, `visual_ex_hp`, and `could_cast_immune`; plus `water_power`, `wax_progress_mgr.cold_wax`, and `wax_progress_mgr.heat_wax`.

## QTE and UI inventory

The general QTE reader first inspects `GK.input`, then attempts `GK.g_ui_mgr.get_ui(battle_world.get_ui_battle_name()).layer_proxy.find_ui('UIBattleQTE')`.

Direct QTE fields are `_in_qte`, `qte_zhizhen`, `good_pos`, `exce_pos`, `qte_hit_flag`, `qte_fail_flag`, `qte_kind`, `qte_uid`, `qte_target_uid`, and `inc_flag`. Invoked UI methods are `is_qte_visible`, `is_qte_visible_test`, `is_in_qte_alert`, `isVisible`, and `getRotation`. The payload transforms the raw ranges into `good_left/right/width` and `perfect_left/right/width` JSON keys.

Musician QTE inspection additionally reads `music_pool`, `qte_mode`, `difficulty`, `music_qte_length_item`, `music_qte_length_ability`, `client_torrence`, `valid_torrence`, `music_seq`, `begin_time`, `channel`, `note_type`, `move_speed`, `offset_adjust`, `node_ui`, `cur_music_scores_id`, `_mode`, `is_show_qte_ui`, and `qte_container_1/2/3`.

## Mode-specific inventory

### Gold Rush

This is the largest feature group. Its explicit field families are:

- Player and health: `ref_attribute_comp`, `ref_game_state_comp`, `ref_hp_comp`, `health`/`cur_hp`, `max_health`/`max_hp`, `health_percent`, `_is_dead`, plus component-derived shield/stamina/game state values.
- Effects: `buff_mgr.buff_dct`, `_buff_list`, `effect_id`/`id`/`_effect_id`, `name_tid`/`_name_tid`, `desc_tid`/`_desc_tid`, `icon_path`/`_icon_path`, `left_time`/`remain_time`, `duration`/`_duration`, `layer`/`_layer`, `is_duration`/`_is_duration`.
- Reward boxes: `_box_id`/`box_id`, `_create_item_info`, `_box_type`/`box_type`, `reward_id`/`_reward_id`, `box_quality`/`_box_quality`, `box_level`/`_box_level`, `_class_level`, `_display_item_quality`, `_gm_item_id`, `_gm_is_mega_gold`, `_gm_display_quality`, `_override_reward_id`, `_ban_pre_open`, `room_index`/`_room_index`, `model_name`/`_model_name`, `_box_data`, `_pending_result_items`, `_slot_items`, `_slot_grid_indices`, `_box_state`, `_open_player_id`, `_open_timer`, `_reset_timer`, and `_unlock_timer`.
- Drops, crafting, and camps: `carry_data`, `is_mega_gold`, `is_newbie_tag`, `_protect_player_uid`, `_protect_expire_time`, `_protect_duration`, `_source_monster_entity_id`, `_source_alchemy_entity_id`, `model_path`, `_operator_uid`, `_is_done`, `_is_synthesizing`, `_pending_target_quality`, `_camp_ui_data`, `_dest_pos`, `_enter_camp`, `_camp_ui_id`, `_enabled`, `_cd_pre_blocked`, `_dest_euler_y`, and `_destroyed_by_butcher`.
- Monsters: `monster_id`, `monster_type`, `monster_level`, `data`, `ref_state_machine`, `current_state`/`cur_state`/`state`, `_anim_name`/`anim_name`, `_anim_rate`/`anim_rate`, `_anim_loop`/`anim_loop`.
- Rooms and geometry: `maze_object.rooms`, `room_id`, `room_index`/`index`, `room_type`, `config_id`/`cfg_id`, `room_data`, `scene_data`, `cfg`, `room_cfg`, `voxels`, `paths`, `path_wall_infos`, `cannot_teleport`, `group_names`, and `model_name_to_data`.
- Interactables and teleporters: `_template_id`, `_state_comp`, `_door_id`, `_model_path`, `_interact_hint`, `_interact_range`, `_room_id`, `_room_index`, `_linknode_id`, `target_area`, `inter_pos`, `message`, `_label`, and `ui_index`.
- Navigation: `path_points`, `target_pos`, `player_pos`, `_smooth_points`, `head_distance`, `is_finished`, and the `GoldRushGuideLineTrail` component.

### Copycat / social-deduction mode

The role reader searches component dictionaries and reads `identity_id`/`role_type`, `camp_id`/`camp`, and `idx`/`seat_idx`/`index`. Role-specific component values are `tar_uid`, `has_guessed_identity`, `mark_uid`/`target_uid`, `bullet_count`, `guess_right_count`, and `infect_uids`. Meeting state uses `sys_handler`, `scene_type`, and `round_count`.

### Character-specific collectors

Explicit special-unit fields include `is_yidhra_puppet`, `_host_uid`, `_master_uid`, `puppet_index`, `out_photo_uid`, `presence_num`, `blackgoat_show_shadow`, `l_skill.jump_shadow_speed`, `jump_speed`, `jump_time`, `prophet_mgr.behavior_info`, `violin_note_mgr`, and `casting_indicator` / `visual.casting_indicator`.

## Why this is materially plaintext

The source is not merely a short configuration table. It contains its own field compatibility logic, semantic mappings, event names, packet structures, data-table names, UI paths, debug labels, and fallback chains. A recipient with only the extracted file can reproduce this inventory using text tools; no decryption key, remote service, or proprietary symbol file is needed.

The remaining uncertainty is runtime compatibility, not readability. Static source cannot establish that an attribute is valid in a later hotfix, or that a semantic label is correct in all states. Those claims require a separately authorized, non-invasive observation and should be reported as such.

## Exhaustive literal catalog

The grouped analysis above is supplemented by the complete, mechanically generated list of every literal attribute name in [literal-field-catalog.md](literal-field-catalog.md). It contains all 394 unique names, including engine fields, method names, enum members, payload hook markers, and compatibility fallbacks; it should be consulted when a field must not be omitted.
