import { promisify } from 'util'
import { read as fsRead } from 'fs'
import { BindingsError } from './errors'
import debugFactory from 'debug'
import { LinuxPortBinding } from './linux'
import { DarwinPortBinding } from './darwin'

const logger = debugFactory('serialport/bindings-cpp/unixRead')
const readAsync = promisify(fsRead)

const readable = (binding: LinuxPortBinding | DarwinPortBinding) => {
  return new Promise<void>((resolve, reject) => {
    if (!binding.poller) {
      throw new Error('No poller on bindings')
    }
    binding.poller.once('readable', err => (err ? reject(err) : resolve()))
  })
}

interface UnixReadOptions {
  binding: LinuxPortBinding | DarwinPortBinding
  buffer: Buffer
  offset: number
  length: number
  fsReadAsync?: typeof readAsync
}

export const unixRead = async ({
  binding,
  buffer,
  offset,
  length,
  fsReadAsync = readAsync,
}: UnixReadOptions): Promise<{ buffer: Buffer; bytesRead: number }> => {
  logger('Starting read')
  if (!binding.isOpen || !binding.fd) {
    throw new BindingsError('Port is not open', { canceled: true })
  }

  // eslint-disable-next-line no-constant-condition
  while (true) {
    try {
      const {bytesRead} = await fsReadAsync(binding.fd, buffer, offset, length, null)
      if (bytesRead === 0) {
        await new Promise((resolve) => setTimeout(resolve, 10))
        continue
      }
      logger('Finished read', bytesRead, 'bytes')
      return {bytesRead, buffer}
    } catch (err) {
      logger('read error', err)
      if (err.code === 'EAGAIN' || err.code === 'EWOULDBLOCK' || err.code === 'EINTR') {
        if (!binding.isOpen) {
          throw new BindingsError('Port is not open', {canceled: true})
        }
        logger('waiting for readable because of code:', err.code)
        await readable(binding)
        continue
      }

      const disconnectError =
        err.code === 'EBADF' || // Bad file number means we got closed
        err.code === 'ENXIO' || // No such device or address probably usb disconnect
        err.code === 'UNKNOWN' ||
        err.errno === -1 // generic error

      if (disconnectError) {
        err.disconnect = true
        logger('disconnecting', err)
      }

      throw err
    }
  }
}
