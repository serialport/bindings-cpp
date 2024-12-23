import { join } from 'path'
import nodeGypBuild from 'node-gyp-build'

// eslint-disable-next-line @typescript-eslint/no-explicit-any
export const binding = nodeGypBuild(join(__dirname, '../')) as any
