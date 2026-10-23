# Copyright (c) 2026 Фешин Виталий Александрович / Vitaly Alexandrovich Feshin.
# E-mail: feshin2004@mail.ru, vfeshin@mindcopy.ru. Тел.: +7(918)345-75-18.

"""behave environment — before_all / after_all hooks for MindCopy acceptance tests."""

import os
import subprocess
import signal
import time
import sys

MINDCOPY_ROOT = os.environ.get("MINDCOPY_ROOT",
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))

CORE_BUILD  = os.path.join(MINDCOPY_ROOT, "BioNeuronCore", "build")
STUDIO_BUILD = os.path.join(MINDCOPY_ROOT, "BioNeuronStudio", "build")
ORCH_BUILD  = os.path.join(MINDCOPY_ROOT, "BioNeuronOrchestrator", "build")

# Global state shared across steps
_ctx = {
    "mindcopy_root": MINDCOPY_ROOT,
    "core_build": CORE_BUILD,
    "studio_build": STUDIO_BUILD,
    "orch_build": ORCH_BUILD,
    "daemon": None,
    "orchestrator": None,
    "agent": None,
    "last_response": None,
    "last_stdout": "",
    "generated_network": None,
}


def before_all(context):
    context.mindcopy_root = MINDCOPY_ROOT
    context.core_build = CORE_BUILD
    context.studio_build = STUDIO_BUILD
    context.orch_build = ORCH_BUILD
    context.daemon = None
    context.orchestrator = None
    context.agent = None
    context.last_response = None
    context.last_stdout = ""

    # Verify prerequisites exist
    _check_prerequisites()


def after_all(context):
    _cleanup_processes()


def _check_prerequisites():
    """Check that required binaries exist, print warnings if not."""
    required = {
        "biocore-daemon": os.path.join(CORE_BUILD, "biocore-daemon"),
        "orchestrator-server": os.path.join(ORCH_BUILD, "orchestrator-server"),
        "biocore-agent": os.path.join(ORCH_BUILD, "biocore-agent"),
        "sim_tadpole_stages": os.path.join(STUDIO_BUILD, "tests", "sim_tadpole_stages"),
        "sim_security": os.path.join(STUDIO_BUILD, "tests", "sim_security"),
        "sim_chaos": os.path.join(STUDIO_BUILD, "tests", "sim_chaos"),
    }
    missing = []
    for name, path in required.items():
        if not os.path.exists(path):
            missing.append(f"{name}: {path}")
    if missing:
        print(f"WARNING: {len(missing)} prerequisite(s) not found:", file=sys.stderr)
        for m in missing:
            print(f"  - {m}", file=sys.stderr)


def _cleanup_processes():
    for proc_name in ["daemon", "orchestrator", "agent"]:
        proc = _ctx.get(proc_name)
        if proc and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
