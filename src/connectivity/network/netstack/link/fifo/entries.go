// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fifo

import (
	"math"
	"math/bits"
)

// Entries provides basic logic used in link devices to operate on queues of
// entries that can be in one of three states:
// - queued: entries describing buffers already operated on, queued to be sent
// to the driver (populated buffers for Tx, already processed buffers for Rx).
// - in-flight: entries describing buffers currently owned by the driver, not
// yet returned.
// - ready: entries describing buffers retrieved from the driver, but not yet
// operated on (free unpopulated buffers for Tx, buffers containing inbound
// traffic for Rx).
type Entries struct {
	// sent, queued, readied are indices modulo (capacity << 1). They
	// implement a ring buffer with 3 regions:
	//
	// - sent:queued:    "queued" entries
	// - queued:readied: "ready" entries
	// - readied:sent:   "in-flight" entries
	//
	// boundary conditions:
	// - readied == sent:   all entries are "in-flight" (implies == queued).
	// - sent == queued:    0 queued entries.
	// - queued == readied: 0 ready entries.
	sent, queued, readied uint16
	capacity              uint16
}

// Init initializes entries with a given capacity rounded up to the next power
// of two and limited to 2^15. Returns the adopted capacity. After Init, all the
// entries are in the "in-flight" state.
func (e *Entries) Init(capacity uint16) uint16 {
	if capacity != 0 {
		// Round up to power of 2.
		capacity = 1 << bits.Len16(capacity-1)
	}
	if capacity > (math.MaxUint16>>1)+1 {
		// Limit range to 2^15.
		capacity >>= 1
	}
	*e = Entries{
		capacity: capacity,
	}
	return capacity
}

// mask masks an index to the range [0, capacity).
func (e *Entries) mask(val uint16) uint16 {
	return val & (e.capacity - 1)
}

// mask2 masks an index to the range [0, capacity*2).
func (e *Entries) mask2(val uint16) uint16 {
	return val & ((e.capacity << 1) - 1)
}

// IncrementSent marks delta entries as moved from "queued" to "in-flight".
// Delta must be limited to the number of queued entries, otherwise Entries may
// become inconsistent.
func (e *Entries) IncrementSent(delta uint16) {
	e.sent = e.mask2(e.sent + delta)
}

// IncrementQueued marks delta entries as moved from "ready" to "queued".
// Delta must be limited to the number of ready entries, otherwise Entries may
// become inconsistent.
func (e *Entries) IncrementQueued(delta uint16) {
	e.queued = e.mask2(e.queued + delta)
}

// IncrementQueued marks delta entries as moved from "in-flight" to "ready".
// Delta must be limited to the number of in-flight buffers, which is the
// size of the range returned by GetInFlightRange.
func (e *Entries) IncrementReadied(delta uint16) {
	e.readied = e.mask2(e.readied + delta)
}

// HaveQueued returns true if there are entries in the "queued" state.
func (e *Entries) HaveQueued() bool {
	return e.sent != e.queued
}

// GetReadiedIndex returns the index of the first ready entry. Only valid if
// HaveReadied is true.
func (e *Entries) GetReadiedIndex() uint16 {
	return e.mask(e.queued)
}

// HaveReadied returns true if there are entries in the "ready" state.
func (e *Entries) HaveReadied() bool {
	return e.queued != e.readied
}

// InFlight returns the number of buffers entries in flight (owned by the
// driver).
func (e *Entries) InFlight() uint16 {
	if readied, sent := e.GetInFlightRange(); readied < sent {
		return sent - readied
	} else {
		return e.capacity - (readied - sent)
	}
}

// GetInFlightRange returns the range of indices for entries in "in-flight"
// state that can be move to readied. The end of the range is always exclusive.
// If range start that is larger than or equal to the range end, it must be
// interpreted as two ranges: (start:) and (:end) as opposed to (start:end).
func (e *Entries) GetInFlightRange() (uint16, uint16) {
	if readied, sent := e.mask(e.readied), e.mask(e.sent); readied == sent && e.sent != e.readied {
		return e.capacity, 0
	} else {
		return readied, sent
	}
}

// GetQueuedRange returns the range of indices for entries in "queued" state.
// The end of the range is always exclusive. If range start that is larger than
// or equal to the range end, it must be interpreted as two ranges: (start:) and
// (:end) as opposed to (start:end).
func (e *Entries) GetQueuedRange() (uint16, uint16) {
	if e.sent == e.queued {
		return e.capacity, 0
	}
	return e.mask(e.sent), e.mask(e.queued)
}
