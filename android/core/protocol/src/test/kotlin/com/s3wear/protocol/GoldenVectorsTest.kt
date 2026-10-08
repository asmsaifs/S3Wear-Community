package com.s3wear.protocol

import com.s3wear.protocol.v1.Ack
import com.s3wear.protocol.v1.Envelope
import com.s3wear.protocol.v1.Hello
import com.s3wear.protocol.v1.HelloAck
import com.s3wear.protocol.v1.Status
import com.s3wear.protocol.v1.TimeSync
import com.squareup.moshi.Moshi
import com.squareup.wire.ProtoAdapter
import com.squareup.wire.WireJsonAdapterFactory
import java.io.File
import org.junit.jupiter.api.Assertions.assertArrayEquals
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.DynamicTest
import org.junit.jupiter.api.TestFactory

/**
 * Protocol golden vectors (protocol/testvectors): for each `<message>_<case>.json`,
 * decode the `.bin` with Wire, compare with the `.json` (proto3 JSON), re-encode
 * and compare the bytes. The firmware host tests run the same vectors with nanopb.
 */
class GoldenVectorsTest {
    private val vectorsDir = File(requireNotNull(System.getProperty("s3w.testvectors")))
    private val moshi = Moshi.Builder().add(WireJsonAdapterFactory()).build()

    /** snake_case message name → adapter; vector names start with one of these. */
    private val messages: Map<String, ProtoAdapter<*>> =
        mapOf(
            "envelope" to Envelope.ADAPTER,
            "status" to Status.ADAPTER,
            "hello" to Hello.ADAPTER,
            "hello_ack" to HelloAck.ADAPTER,
            "time_sync" to TimeSync.ADAPTER,
            "ack" to Ack.ADAPTER
        )

    @TestFactory
    fun goldenVectors(): List<DynamicTest> {
        val names =
            vectorsDir
                .listFiles { f -> f.extension == "json" }
                .orEmpty()
                .map { it.nameWithoutExtension }
                .sorted()
        assertTrue(names.isNotEmpty(), "no vectors in $vectorsDir")
        return names.map { name -> DynamicTest.dynamicTest(name) { roundTrip(name) } }
    }

    private fun roundTrip(name: String) {
        @Suppress("UNCHECKED_CAST")
        val adapter = adapterFor(name) as ProtoAdapter<Any>
        val bin = File(vectorsDir, "$name.bin").readBytes()
        val json = File(vectorsDir, "$name.json").readText()

        val fromBin = adapter.decode(bin)
        val fromJson = moshi.adapter<Any>(requireNotNull(adapter.type).javaObjectType).fromJson(json)

        assertEquals(fromJson, fromBin, "$name: .bin and .json differ")
        assertArrayEquals(bin, adapter.encode(fromBin), "$name: re-encoded bytes differ")
    }

    /** Longest matching prefix wins, so `hello_ack_x` is a HelloAck, not a Hello. */
    private fun adapterFor(name: String): ProtoAdapter<*> {
        val key =
            messages.keys
                .filter { name == it || name.startsWith("${it}_") }
                .maxByOrNull { it.length }
        return messages[requireNotNull(key) { "$name: no message named like this" }]!!
    }
}
