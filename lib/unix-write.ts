import { write } from 'fs'
import debugFactory from 'debug'
import { promisify } from 'util'
import { LinuxPortBinding } from './linux'
import { DarwinPortBinding } from './darwin'
const logger = debugFactory('serialport/bindings-cpp/unixWrite')

const writeAsync = promisify(write)

const writable = (binding: LinuxPortBinding | DarwinPortBinding) => {
  return new Promise<void>((resolve, reject) => {
    binding.poller.once('writable', err => (err ? reject(err) : resolve()))
  })
}

interface UnixWriteOptions {
  binding: LinuxPortBinding | DarwinPortBinding
  buffer: Buffer
  offset?: number
  fsWriteAsync?: typeof writeAsync
}

export const unixWrite = async ({ binding, buffer, offset = 0, fsWriteAsync = writeAsync }: UnixWriteOptions): Promise<void> => {
  const bytesToWrite = buffer.length - offset
  logger('Starting write', buffer.length, 'bytes offset', offset, 'bytesToWrite', bytesToWrite)
  if (!binding.isOpen || !binding.fd) {
    throw new Error('Port is not open')
  }
  try {
    const { bytesWritten } = await fsWriteAsync(binding.fd, buffer, offset, bytesToWrite)
    logger('write returned: wrote', bytesWritten, 'bytes')
    if (bytesWritten + offset < buffer.length) {
      if (!binding.isOpen) {
        throw new Error('Port is not open')
      }
      return unixWrite({ binding, buffer, offset: bytesWritten + offset, fsWriteAsync })
    }

    logger('Finished writing', bytesWritten + offset, 'bytes')
  } catch (err) {
    logger('write errored', err)
    if (err.code === 'EAGAIN' || err.code === 'EWOULDBLOCK' || err.code === 'EINTR') {
      if (!binding.isOpen) {
        throw new Error('Port is not open')
      }
      logger('waiting for writable because of code:', err.code)
      await writable(binding)
      return unixWrite({ binding, buffer, offset, fsWriteAsync })
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

    logger('error', err)
    throw err
  }
}
