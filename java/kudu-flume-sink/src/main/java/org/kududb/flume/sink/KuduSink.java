/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
package org.kududb.flume.sink;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Preconditions;
import com.google.common.base.Throwables;
import org.apache.flume.Channel;
import org.apache.flume.Context;
import org.apache.flume.Event;
import org.apache.flume.EventDeliveryException;
import org.apache.flume.FlumeException;
import org.apache.flume.Transaction;
import org.apache.flume.conf.Configurable;
import org.apache.flume.instrumentation.SinkCounter;
import org.apache.flume.sink.AbstractSink;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;
import org.kududb.client.AsyncKuduClient;
import org.kududb.client.KuduClient;
import org.kududb.client.KuduSession;
import org.kududb.client.KuduTable;
import org.kududb.client.Operation;
import org.kududb.client.OperationResponse;
import org.kududb.client.SessionConfiguration;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.List;

/**
 * A simple sink which reads events from a channel and writes them to Kudu.
 * To use this sink, it has to be configured with certain mandatory parameters:<p>
 * <tt>tableName: </tt> The name of the table in Kudu to write to. <p>
 * <tt>masterAddresses: </tt> Comma-separated list of "host:port" pairs of the masters (port optional). <p>
 */
@InterfaceAudience.Public
@InterfaceStability.Evolving
public class KuduSink extends AbstractSink implements Configurable {
  private static final Logger logger = LoggerFactory.getLogger(KuduSink.class);
  private static final Long DEFAULT_BATCH_SIZE = 100L;
  private static final Long DEFAULT_TIMEOUT_MILLIS =
          AsyncKuduClient.DEFAULT_OPERATION_TIMEOUT_MS;
  private static final String DEFAULT_KUDU_EVENT_PRODUCER =
          "org.kududb.flume.sink.SimpleKuduEventProducer";
  private static final boolean DEFAULT_IGNORE_DUPLICATE_ROWS = true;

  private String masterAddresses;
  private String tableName;
  private long batchSize;
  private long timeoutMillis;
  private boolean ignoreDuplicateRows;
  private KuduTable table;
  private KuduSession session;
  private KuduClient client;
  private KuduEventProducer eventProducer;
  private String eventProducerType;
  private Context producerContext;
  private SinkCounter sinkCounter;

  public KuduSink() {
    this(null);
  }

  @VisibleForTesting
  @InterfaceAudience.Private
  public KuduSink(KuduClient kuduClient) {
    this.client = kuduClient;
  }

  @Override
  public void start() {
    Preconditions.checkState(table == null && session == null, "Please call stop " +
        "before calling start on an old instance.");

    // This is not null only inside tests
    if (client == null) {
      client = new KuduClient.KuduClientBuilder(masterAddresses).build();
    }
    session = client.newSession();
    session.setFlushMode(SessionConfiguration.FlushMode.MANUAL_FLUSH);
    session.setTimeoutMillis(timeoutMillis);
    session.setIgnoreAllDuplicateRows(ignoreDuplicateRows);

    try {
      table = client.openTable(tableName);
    } catch (Exception e) {
      sinkCounter.incrementConnectionFailedCount();
      String msg = String.format("Could not open table '%s' from Kudu", tableName);
      logger.error(msg, e);
      throw new FlumeException(msg, e);
    }

    super.start();
    sinkCounter.incrementConnectionCreatedCount();
    sinkCounter.start();
  }

  @Override
  public void stop() {
    try {
      if (client != null) {
        client.shutdown();
      }
      client = null;
      table = null;
      session = null;
    } catch (Exception e) {
      throw new FlumeException("Error closing client.", e);
    }
    sinkCounter.incrementConnectionClosedCount();
    sinkCounter.stop();
  }

  @SuppressWarnings("unchecked")
  @Override
  public void configure(Context context) {
    masterAddresses = context.getString(KuduSinkConfigurationConstants.MASTER_ADDRESSES);
    tableName = context.getString(KuduSinkConfigurationConstants.TABLE_NAME);

    batchSize = context.getLong(
            KuduSinkConfigurationConstants.BATCH_SIZE, DEFAULT_BATCH_SIZE);
    timeoutMillis = context.getLong(
            KuduSinkConfigurationConstants.TIMEOUT_MILLIS, DEFAULT_TIMEOUT_MILLIS);
    ignoreDuplicateRows = context.getBoolean(
            KuduSinkConfigurationConstants.IGNORE_DUPLICATE_ROWS, DEFAULT_IGNORE_DUPLICATE_ROWS);
    eventProducerType = context.getString(KuduSinkConfigurationConstants.PRODUCER);

    Preconditions.checkNotNull(masterAddresses,
        "Master address cannot be empty, please specify '" +
                KuduSinkConfigurationConstants.MASTER_ADDRESSES +
                "' in configuration file");
    Preconditions.checkNotNull(tableName,
        "Table name cannot be empty, please specify '" +
                KuduSinkConfigurationConstants.TABLE_NAME +
                "' in configuration file");

    // Check for event producer, if null set event producer type.
    if (eventProducerType == null || eventProducerType.isEmpty()) {
      eventProducerType = DEFAULT_KUDU_EVENT_PRODUCER;
      logger.info("No Kudu event producer defined, will use default");
    }

    producerContext = new Context();
    producerContext.putAll(context.getSubProperties(
            KuduSinkConfigurationConstants.PRODUCER_PREFIX));

    try {
      Class<? extends KuduEventProducer> clazz =
          (Class<? extends KuduEventProducer>)
          Class.forName(eventProducerType);
      eventProducer = clazz.newInstance();
      eventProducer.configure(producerContext);
    } catch (Exception e) {
      logger.error("Could not instantiate Kudu event producer." , e);
      Throwables.propagate(e);
    }
    sinkCounter = new SinkCounter(this.getName());
  }

  public KuduClient getClient() {
    return client;
  }

  @Override
  public Status process() throws EventDeliveryException {
    if (session.hasPendingOperations()) {
      // If for whatever reason we have pending operations then just refuse to process
      // and tell caller to try again a bit later. We don't want to pile on the kudu
      // session object.
      return Status.BACKOFF;
    }

    Channel channel = getChannel();
    Transaction txn = channel.getTransaction();

    txn.begin();

    try {
      long txnEventCount = 0;
      for (; txnEventCount < batchSize; txnEventCount++) {
        Event event = channel.take();
        if (event == null) {
          break;
        }

        eventProducer.initialize(event, table);
        List<Operation> operations = eventProducer.getOperations();
        for (Operation o : operations) {
          session.apply(o);
        }
      }

      logger.debug("Flushing {} events", txnEventCount);
      List<OperationResponse> responses = session.flush();
      if (responses != null) {
        for (OperationResponse response : responses) {
          // Only throw an EventDeliveryException if at least one of the responses was
          // not a row error. Row errors can occur for example when an event is inserted
          // into Kudu successfully as part of a transaction but the transaction is
          // rolled back and a subsequent replay of the Flume transaction leads to a
          // row error due to the fact that the row already exists (Kudu doesn't support
          // "insert or overwrite" semantics yet).
          if (response.hasRowError()) {
            throw new EventDeliveryException("Failed to flush one or more changes. " +
                "Transaction rolled back: " + response.getRowError().toString());
          }
        }
      }

      if (txnEventCount == 0) {
        sinkCounter.incrementBatchEmptyCount();
      } else if (txnEventCount == batchSize) {
        sinkCounter.incrementBatchCompleteCount();
      } else {
        sinkCounter.incrementBatchUnderflowCount();
      }

      txn.commit();

      if (txnEventCount == 0) {
        return Status.BACKOFF;
      }

      sinkCounter.addToEventDrainSuccessCount(txnEventCount);
      return Status.READY;

    } catch (Throwable e) {
      txn.rollback();

      String msg = "Failed to commit transaction. Transaction rolled back.";
      logger.error(msg, e);
      if (e instanceof Error || e instanceof RuntimeException) {
        Throwables.propagate(e);
      } else {
        logger.error(msg, e);
        throw new EventDeliveryException(msg, e);
      }
    } finally {
      txn.close();
    }

    return Status.BACKOFF;
  }

  @VisibleForTesting
  @InterfaceAudience.Private
  KuduEventProducer getEventProducer() {
    return eventProducer;
  }
}
