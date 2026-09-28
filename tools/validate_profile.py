#!/usr/bin/env python3
# 独立实现：本工具为 GalEngineKit 原创实现
# 功能：校验引擎档案 JSON 是否符合 schemas/engine-profile.schema.json
# 用法：python3 tools/validate_profile.py profiles/*.json

import json
import sys
import os

try:
    import jsonschema
except ImportError:
    print("错误：未安装 jsonschema 库。请运行：pip install jsonschema")
    sys.exit(1)


def load_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def validate_profile(profile_path, schema):
    """校验单个引擎档案，返回 (是否通过, 错误信息列表)"""
    errors = []
    try:
        profile = load_json(profile_path)
    except json.JSONDecodeError as e:
        return False, [f"JSON 解析失败：{e}"]
    except Exception as e:
        return False, [f"文件读取失败：{e}"]

    # 1. Schema 校验
    validator = jsonschema.Draft7Validator(schema)
    schema_errors = sorted(validator.iter_errors(profile), key=lambda e: e.path)
    for err in schema_errors:
        path = ".".join(str(p) for p in err.absolute_path) if err.absolute_path else "(root)"
        errors.append(f"[Schema] {path}: {err.message}")

    # 2. 业务一致性校验
    # engine_id 应与文件名一致
    filename_id = os.path.splitext(os.path.basename(profile_path))[0]
    if profile.get("engine_id") != filename_id:
        errors.append(f"[一致性] engine_id '{profile.get('engine_id')}' 与文件名 '{filename_id}' 不一致")

    # freetype 路径不应推荐 wine_registry
    if profile.get("text_rendering_path") == "freetype":
        approach = profile.get("font_strategy", {}).get("recommended_approach")
        if approach == "wine_registry":
            errors.append("[一致性] FreeType 渲染路径下 wine_registry 方案无效，应使用 file_replacement 或 engine_hook")

    # bitmap 路径应推荐 bitmap_atlas
    if profile.get("text_rendering_path") == "bitmap":
        approach = profile.get("font_strategy", {}).get("recommended_approach")
        if approach not in ("bitmap_atlas", "engine_hook"):
            errors.append("[一致性] 位图字体渲染路径应推荐 bitmap_atlas 或 engine_hook")

    # hook_targets 不应包含模拟器内置 API 目标
    for i, target in enumerate(profile.get("hook_targets", [])):
        if target.get("target_type") not in ("engine_runtime", "engine_file_api", "engine_cache"):
            errors.append(f"[一致性] hook_targets[{i}] 目标类型不合法，仅限引擎自身代码目标")

    return len(errors) == 0, errors


def main():
    if len(sys.argv) < 2:
        print("用法：python3 tools/validate_profile.py <profile1.json> [profile2.json ...]")
        print("示例：python3 tools/validate_profile.py profiles/*.json")
        sys.exit(1)

    # 定位 schema 文件
    script_dir = os.path.dirname(os.path.abspath(__file__))
    schema_path = os.path.join(script_dir, "..", "schemas", "engine-profile.schema.json")
    schema_path = os.path.normpath(schema_path)

    if not os.path.exists(schema_path):
        print(f"错误：找不到 schema 文件：{schema_path}")
        sys.exit(1)

    schema = load_json(schema_path)

    total = len(sys.argv) - 1
    passed = 0
    failed = 0

    print(f"GalEngineKit 引擎档案校验工具")
    print(f"Schema: {schema_path}")
    print(f"待校验档案数：{total}")
    print("-" * 60)

    for profile_path in sys.argv[1:]:
        if not os.path.exists(profile_path):
            print(f"[跳过] 文件不存在：{profile_path}")
            continue

        ok, errors = validate_profile(profile_path, schema)
        if ok:
            passed += 1
            print(f"[通过] {profile_path}")
        else:
            failed += 1
            print(f"[失败] {profile_path}")
            for err in errors:
                print(f"       - {err}")

    print("-" * 60)
    print(f"结果：通过 {passed} / 失败 {failed} / 总计 {total}")

    if failed > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
