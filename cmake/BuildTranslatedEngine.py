"""Build the native Mac compatibility worker from pinned, published sources."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import urllib.request
import zipfile

REVISION = '9ca68a48ed92a6bc1cc02d24bf27399aa2a59760'
ARCHIVE_SHA256 = 'f4d89fea4c9b437a2cb6520ec18dce6a6e635cabac3e68376926ef1b191dceb8'


def run(*arguments, cwd=None):
    subprocess.run([str(argument) for argument in arguments], cwd=cwd, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--architectures', default='arm64,x86_64')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--archive', type=Path)
    args = parser.parse_args()
    architectures = args.architectures.split(',')
    if not architectures or any(arch not in ('arm64', 'x86_64') for arch in architectures):
        parser.error('architectures must be arm64 and/or x86_64')
    if args.jobs < 1:
        parser.error('jobs must be positive')
    root = Path(__file__).resolve().parent.parent
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    supplied = root / 'Dependencies/BoxedWine-source.zip'
    archive = args.archive or (supplied if supplied.exists() else build / 'boxedwine-source.zip')
    if not archive.exists():
        with urllib.request.urlopen('https://codeload.github.com/danoon2/Boxedwine/zip/' + REVISION, timeout=60) as source:
            with archive.open('xb') as target:
                shutil.copyfileobj(source, target)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != ARCHIVE_SHA256:
        raise RuntimeError('Pinned BoxedWine source archive differs')
    source = build / ('Boxedwine-' + REVISION)
    if not source.exists():
        with zipfile.ZipFile(archive) as package:
            for entry in package.infolist():
                if not (build / entry.filename).resolve().is_relative_to(build):
                    raise RuntimeError('Unsafe source archive entry')
            package.extractall(build)
    translated = root / 'Source/Translated'
    for replacement in (translated / 'Overrides').rglob('*'):
        if replacement.is_file():
            shutil.copy2(replacement, source / replacement.relative_to(translated / 'Overrides'))
    shutil.copy2(translated / 'SDL_config_macosx.h', source / 'lib/sdl2/include/SDL_config_macosx.h')
    shutil.copy2(translated / 'AudioRuntimeStubs.cpp', build / 'BoxedWineIOSStubs.cpp')
    shutil.copy2(root / 'Source/WorkerScheduling.h', build / 'WorkerScheduling.h')
    recipe = (translated / 'project.yml.in').read_text().replace('@BOXEDWINE_SOURCE@', str(source)).replace('@BUILD_ROOT@', str(build))
    (build / 'project.yml').write_text(recipe)
    run('xcodegen', 'generate', '--spec', build / 'project.yml', cwd=build)
    sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
    outputs = []
    for architecture in architectures:
        if architecture == 'x86_64':
            output = build / 'SoundCanvasHelper-x86_64'
            run('xcrun', 'clang++', '-std=c++17', '-O2', '-arch', architecture,
                '-isysroot', sdk, '-mmacosx-version-min=12.0', '-ffile-prefix-map=' + str(root) + '=.',
                root / 'Source/Helper.cpp', '-o', output)
            outputs.append(output)
            continue
        derived = build / ('Build-' + architecture)
        run('xcodebuild', '-project', build / 'BoxedWineMacCore.xcodeproj', '-scheme', 'BoxedWineMacCore',
            '-configuration', 'Release', '-derivedDataPath', derived, '-jobs', args.jobs,
            'ARCHS=' + architecture, 'GCC_OPTIMIZATION_LEVEL=3', 'LLVM_LTO=YES_THIN', 'CODE_SIGNING_ALLOWED=NO', 'build', cwd=build)
        run('xcodebuild', '-project', build / 'BoxedWineMacCore.xcodeproj', '-scheme', 'SDLHeadless',
            '-configuration', 'Release', '-derivedDataPath', derived, '-jobs', args.jobs,
            'ARCHS=' + architecture, 'GCC_OPTIMIZATION_LEVEL=3', 'LLVM_LTO=YES_THIN', 'CODE_SIGNING_ALLOWED=NO', 'build', cwd=build)
        products = derived / 'Build/Products/Release'
        stub = build / ('SDLHeadlessMacStubs-' + architecture + '.o')
        run('xcrun', 'clang', '-O3', '-arch', architecture, '-isysroot', sdk,
            '-mmacosx-version-min=12.0', '-ffile-prefix-map=' + str(root) + '=.',
            '-c', translated / 'SDLHeadlessMacStubs.c', '-o', stub)
        output = build / ('SoundCanvasHelper-' + architecture)
        command = ['xcrun', 'clang++', '-std=c++20', '-O3', '-flto=thin', '-arch', architecture,
                   '-isysroot', sdk, '-mmacosx-version-min=12.0', '-I' + str(root / 'Source'),
                   '-ffile-prefix-map=' + str(root) + '=.', '-ffile-prefix-map=' + str(build) + '=.',
                   str(translated / 'Worker.cpp'), str(products / 'libBoxedWineMacCore.a'),
                   str(products / 'libSDLHeadless.a'), str(stub), '-lz', '-liconv']
        for framework in ['AVFoundation', 'AudioToolbox', 'CoreAudio', 'CoreFoundation',
                          'CoreServices', 'Foundation', 'Security', 'SystemConfiguration']:
            command.extend(['-framework', framework])
        run(*command, '-o', output)
        outputs.append(output)
    run('lipo', '-create', *outputs, '-output', build / 'SoundCanvasHelper')
    print('Built compatibility worker:', build / 'SoundCanvasHelper', flush=True)


if __name__ == '__main__':
    main()
