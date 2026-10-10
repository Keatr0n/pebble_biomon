#
# This file is the default set of rules to compile a Pebble project.
#
# Feel free to customize this to your needs.
#

import os.path
try:
    from sh import CommandNotFound, jshint, cat, ErrorReturnCode_2
    hint = jshint
except (ImportError, Exception):
    hint = None

top = '.'
out = 'build'


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    ctx.load('pebble_sdk')


def build(ctx):
    if False and hint is not None:
        try:
            hint(['--config', 'pebble-jshintrc'] + [node.abspath() for node in ctx.path.ant_glob("src/**/*.js")], _tty_out=False) # no tty because there are none in the cloudpebble sandbox.
        except ErrorReturnCode_2 as e:
            ctx.fatal("\nJavaScript linting failed (you can disable this in Project Settings):\n" + e.stdout)

    ctx.load('pebble_sdk')

    build_worker = os.path.exists('worker_src')
    binaries = []

    for p in ctx.env.TARGET_PLATFORMS:
        ctx.set_env(ctx.all_envs[p])
        ctx.set_group(ctx.env.PLATFORM_NAME)
        # BIOMON_DEMO=1 (healthy) or 2 (alerts) swaps sensor readings for canned ones,
        # so the emulator can show a fully populated face.
        if os.environ.get('BIOMON_DEMO'):
            ctx.env.append_value('DEFINES', 'BIOMON_DEMO=' + os.environ['BIOMON_DEMO'])
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        # shared/ holds the measuring logic that both the app and its background worker run.
        shared = ctx.path.ant_glob('shared/**/*.c')
        ctx.pbl_program(source=ctx.path.ant_glob('src/c/**/*.c') + shared, includes=['shared'],
                        target=app_elf)

        if build_worker:
            worker_elf = '{}/pebble-worker.elf'.format(ctx.env.BUILD_DIR)
            binaries.append({'platform': p, 'app_elf': app_elf, 'worker_elf': worker_elf})
            ctx.pbl_worker(source=ctx.path.ant_glob('worker_src/c/**/*.c') + shared,
                           includes=['shared'], defines=['BIOMON_WORKER'], target=worker_elf)
        else:
            binaries.append({'platform': p, 'app_elf': app_elf})

    ctx(features='subst',
        source='package.json',
        target='js/package.json',
        is_copy=True)

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries, js=ctx.path.ant_glob(['src/pkjs/**/*.js', 'src/pkjs/**/*.json']), js_entry_file='src/pkjs/index.js')
