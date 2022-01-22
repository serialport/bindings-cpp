/* eslint-disable @typescript-eslint/no-var-requires */
import debugFactory from 'debug'
import { DarwinBinding } from './darwin'
import { LinuxBinding } from './linux'
import { WindowsBinding } from './win32'
const debug = debugFactory('serialport/bindings-cpp')

export * from './darwin'
export * from './linux'
export * from './win32'
export * from './binding-interface'

export type AllBindingClasses = typeof WindowsBinding | typeof DarwinBinding | typeof LinuxBinding
export type AllBindings = WindowsBinding | DarwinBinding | LinuxBinding
let binding: AllBindingClasses
switch (process.platform) {
case 'win32':
  debug('loading WindowsBinding')
  binding = WindowsBinding
  break
case 'darwin':
  debug('loading DarwinBinding')
  binding = DarwinBinding
  break
default:
  debug('loading LinuxBinding')
  binding = LinuxBinding
}

export default binding
